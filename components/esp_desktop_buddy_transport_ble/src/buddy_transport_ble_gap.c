/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "buddy_transport_ble_internal.h"

static const char *TAG = "esp_desktop_buddy_transport_ble";

static const ble_uuid128_t BUDDY_NUS_SERVICE_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_PAIRING_TIMEOUT_US (60LL * 1000 * 1000)
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_SUBSCRIBE_TIMEOUT_US (15LL * 1000 * 1000)
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_RX_IDLE_TIMEOUT_US (45LL * 1000 * 1000)

_Static_assert(ESP_DESKTOP_BUDDY_TRANSPORT_BLE_PAIRING_TIMEOUT_US >
                   ESP_DESKTOP_BUDDY_TRANSPORT_BLE_SUBSCRIBE_TIMEOUT_US,
               "pairing must have more time than post-encryption subscription");
_Static_assert(ESP_DESKTOP_BUDDY_TRANSPORT_BLE_RX_IDLE_TIMEOUT_US >
                   ESP_DESKTOP_BUDDY_TRANSPORT_BLE_SUBSCRIBE_TIMEOUT_US,
               "active RX idle timeout must exceed subscription setup timeout");

typedef enum {
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_PAIRING = 0,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_SUBSCRIBE,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_RX_IDLE,
} esp_desktop_buddy_transport_ble_stale_phase_t;

static int esp_desktop_buddy_transport_ble_gap_event(struct ble_gap_event *event, void *arg);
static ble_npl_error_t esp_desktop_buddy_transport_ble_delay_us_to_ticks(
    uint64_t delay_us,
    ble_npl_time_t *out_ticks);

static void esp_desktop_buddy_transport_ble_record_conn_params(
    esp_desktop_buddy_transport_ble_t *transport,
    uint16_t conn_handle,
    bool is_update)
{
    struct ble_gap_conn_desc desc;
    int rc;

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_conn_find failed rc=%d while reading connection parameters", rc);
        return;
    }

    xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
    transport->diagnostics.conn_params_valid = true;
    transport->diagnostics.conn_interval_units = desc.conn_itvl;
    transport->diagnostics.slave_latency = desc.conn_latency;
    transport->diagnostics.supervision_timeout_units = desc.supervision_timeout;
    if (is_update) {
        transport->diagnostics.conn_param_update_count++;
    } else {
        transport->restore_conn_params_valid = true;
        transport->restore_conn_interval_units = desc.conn_itvl;
        transport->restore_slave_latency = desc.conn_latency;
        transport->restore_supervision_timeout_units = desc.supervision_timeout;
    }
    xSemaphoreGive(transport->state_mutex);

    ESP_LOGI(TAG,
             "connection params%s interval=%u (%.2fms) latency=%u timeout=%ums",
             is_update ? " updated" : "",
             desc.conn_itvl,
             (double)desc.conn_itvl * 1.25,
             desc.conn_latency,
             (unsigned)desc.supervision_timeout * 10);
}

static esp_desktop_buddy_transport_ble_stale_phase_t
esp_desktop_buddy_transport_ble_stale_phase_locked(
    const esp_desktop_buddy_transport_ble_t *transport,
    int64_t *started_us,
    int64_t *timeout_us)
{
    esp_desktop_buddy_transport_ble_stale_phase_t phase;

    if (!transport->state.encrypted) {
        phase = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_PAIRING;
        *started_us = transport->stale_phase_started_us;
        *timeout_us = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_PAIRING_TIMEOUT_US;
    } else if (!transport->state.subscribed) {
        phase = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_SUBSCRIBE;
        *started_us = transport->stale_phase_started_us;
        *timeout_us = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_SUBSCRIBE_TIMEOUT_US;
    } else {
        phase = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_RX_IDLE;
        *started_us = transport->last_rx_us;
        *timeout_us = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_RX_IDLE_TIMEOUT_US;
    }
    return phase;
}

static const char *esp_desktop_buddy_transport_ble_stale_phase_name(
    esp_desktop_buddy_transport_ble_stale_phase_t phase)
{
    switch (phase) {
    case ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_PAIRING:
        return "pairing";
    case ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_SUBSCRIBE:
        return "subscribe";
    case ESP_DESKTOP_BUDDY_TRANSPORT_BLE_STALE_PHASE_RX_IDLE:
        return "rx_idle";
    default:
        return "unknown";
    }
}

static void esp_desktop_buddy_transport_ble_stale_link_callout_cb(struct ble_npl_event *event)
{
    esp_desktop_buddy_transport_ble_t *transport =
        event != NULL ? (esp_desktop_buddy_transport_ble_t *)ble_npl_event_get_arg(event) : NULL;
    esp_desktop_buddy_transport_ble_stale_phase_t phase;
    uint64_t generation = 0;
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    int64_t started_us;
    int64_t timeout_us;
    int64_t now_us;
    int64_t elapsed_us = 0;
    bool terminate = false;
    int rc;

    if (transport == NULL || transport->state_mutex == NULL) {
        return;
    }

    now_us = esp_timer_get_time();
    xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
    phase = esp_desktop_buddy_transport_ble_stale_phase_locked(transport,
                                                               &started_us,
                                                               &timeout_us);
    terminate = !transport->shutting_down &&
                transport->state.connected &&
                transport->conn_handle != BLE_HS_CONN_HANDLE_NONE &&
                now_us > started_us &&
                now_us - started_us >= timeout_us;
    if (terminate) {
        generation = transport->link_generation;
        conn_handle = transport->conn_handle;
        elapsed_us = now_us - started_us;
    }
    xSemaphoreGive(transport->state_mutex);

    if (terminate) {
        ble_npl_time_t retry_ticks;

        ESP_LOGW(TAG,
                 "terminating stale BLE link generation=%llu conn_handle=%u phase=%s elapsed=%lldms",
                 (unsigned long long)generation,
                 conn_handle,
                 esp_desktop_buddy_transport_ble_stale_phase_name(phase),
                 (long long)(elapsed_us / 1000));
        rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN && rc != BLE_HS_EALREADY) {
            bool reset_failed = false;

            ESP_LOGW(TAG,
                     "ble_gap_terminate failed rc=%d for stale link; retrying in 1000ms",
                     rc);
            if (esp_desktop_buddy_transport_ble_delay_us_to_ticks(1000ULL * 1000,
                                                                   &retry_ticks) != BLE_NPL_OK) {
                ESP_LOGW(TAG, "failed to schedule stale BLE terminate retry");
                return;
            }
            xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
            if (!transport->shutting_down &&
                transport->state.connected &&
                transport->link_generation == generation &&
                transport->conn_handle == conn_handle &&
                transport->stale_link_callout_initialized) {
                reset_failed = ble_npl_callout_reset(&transport->stale_link_callout,
                                                     retry_ticks) != BLE_NPL_OK;
            }
            xSemaphoreGive(transport->state_mutex);
            if (reset_failed) {
                ESP_LOGW(TAG, "failed to schedule stale BLE terminate retry");
            }
        }
    } else {
        esp_desktop_buddy_transport_ble_schedule_stale_link_check(transport);
    }
}

esp_err_t esp_desktop_buddy_transport_ble_init_stale_link_callout(
    esp_desktop_buddy_transport_ble_t *transport)
{
    struct ble_npl_eventq *eventq;
    int rc;

    if (transport == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (transport->stale_link_callout_initialized) {
        return ESP_OK;
    }

    eventq = nimble_port_get_dflt_eventq();
    if (eventq == NULL) {
        return ESP_FAIL;
    }
    rc = ble_npl_callout_init(&transport->stale_link_callout,
                              eventq,
                              esp_desktop_buddy_transport_ble_stale_link_callout_cb,
                              transport);
    if (rc != BLE_NPL_OK) {
        return ESP_FAIL;
    }
    transport->stale_link_callout_initialized = true;
    return ESP_OK;
}

void esp_desktop_buddy_transport_ble_deinit_stale_link_callout(
    esp_desktop_buddy_transport_ble_t *transport)
{
    if (transport == NULL || !transport->stale_link_callout_initialized) {
        return;
    }

    ble_npl_callout_stop(&transport->stale_link_callout);
    ble_npl_callout_deinit(&transport->stale_link_callout);
    transport->stale_link_callout_initialized = false;
}

static ble_npl_error_t esp_desktop_buddy_transport_ble_delay_us_to_ticks(
    uint64_t delay_us,
    ble_npl_time_t *out_ticks)
{
    uint32_t delay_ms = (uint32_t)((delay_us + 999) / 1000);
    uint32_t represented_ms;
    ble_npl_error_t err;

    err = ble_npl_time_ms_to_ticks(delay_ms, out_ticks);
    if (err != BLE_NPL_OK) {
        return err;
    }
    if (*out_ticks == 0) {
        *out_ticks = 1;
    } else if (ble_npl_time_ticks_to_ms(*out_ticks, &represented_ms) == BLE_NPL_OK &&
               represented_ms < delay_ms) {
        (*out_ticks)++;
    }
    return BLE_NPL_OK;
}

void esp_desktop_buddy_transport_ble_schedule_stale_link_check(esp_desktop_buddy_transport_ble_t *transport)
{
    int64_t now_us;
    int64_t started_us;
    int64_t elapsed_us;
    int64_t timeout_us;
    uint64_t delay_us;
    ble_npl_time_t delay_ticks;
    ble_npl_error_t err;

    if (transport == NULL || !transport->stale_link_callout_initialized) {
        return;
    }

    xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
    if (transport->shutting_down ||
        !transport->state.connected ||
        transport->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        xSemaphoreGive(transport->state_mutex);
        ble_npl_callout_stop(&transport->stale_link_callout);
        return;
    }
    now_us = esp_timer_get_time();
    (void)esp_desktop_buddy_transport_ble_stale_phase_locked(transport,
                                                             &started_us,
                                                             &timeout_us);
    elapsed_us = now_us > started_us ? now_us - started_us : 0;
    delay_us = elapsed_us >= timeout_us ? 1 : (uint64_t)(timeout_us - elapsed_us);
    err = esp_desktop_buddy_transport_ble_delay_us_to_ticks(delay_us, &delay_ticks);
    if (err != BLE_NPL_OK ||
        ble_npl_callout_reset(&transport->stale_link_callout, delay_ticks) != BLE_NPL_OK) {
        ESP_LOGW(TAG, "failed to schedule stale BLE link check");
    }
    xSemaphoreGive(transport->state_mutex);
}

void esp_desktop_buddy_transport_ble_cancel_stale_link_check(esp_desktop_buddy_transport_ble_t *transport)
{
    if (transport == NULL || !transport->stale_link_callout_initialized) {
        return;
    }

    ble_npl_callout_stop(&transport->stale_link_callout);
}

static void esp_desktop_buddy_transport_ble_start_advertising(esp_desktop_buddy_transport_ble_t *transport)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    int rc;

    if (transport == NULL || transport->shutting_down) {
        return;
    }

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t[]){ BUDDY_NUS_SERVICE_UUID };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    /* flags 与 128-bit UUID 共占 21 字节，名称 AD 头占 2 字节，正文最多 8 字节。
     * 主广播和扫描响应使用同一名称，UTF-8 前缀不切开多字节字符。 */
    size_t name_len = strlen(transport->advertising_name);
    size_t short_len = name_len > 8 ? 8 : name_len;
    while (short_len > 0 && short_len < name_len &&
           ((uint8_t)transport->advertising_name[short_len] & 0xc0) == 0x80) --short_len;
    fields.name = (uint8_t *)transport->advertising_name;
    fields.name_len = short_len;
    fields.name_is_complete = short_len == name_len;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed rc=%d", rc);
        return;
    }

    rsp_fields.name = (uint8_t *)transport->advertising_name;
    rsp_fields.name_len = strlen(transport->advertising_name);
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed rc=%d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = CONFIG_ESP_DESKTOP_BUDDY_TRANSPORT_BLE_ADV_ITVL_MIN;
    adv_params.itvl_max = CONFIG_ESP_DESKTOP_BUDDY_TRANSPORT_BLE_ADV_ITVL_MAX;

    rc = ble_gap_adv_start(transport->own_addr_type,
                           NULL,
                           BLE_HS_FOREVER,
                           &adv_params,
                           esp_desktop_buddy_transport_ble_gap_event,
                           transport);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "advertising as %s", transport->advertising_name);
}

static int esp_desktop_buddy_transport_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_desktop_buddy_transport_ble_t *transport =
        arg != NULL ? (esp_desktop_buddy_transport_ble_t *)arg : g_esp_desktop_buddy_transport_ble_active;
    esp_desktop_buddy_transport_ble_state_t before;
    esp_desktop_buddy_transport_ble_state_t after;
    struct ble_gap_conn_desc desc;
    uint16_t mtu;
    bool was_tx_ready;
    int rc;

    if (transport == NULL) {
        return 0;
    }

    was_tx_ready = esp_desktop_buddy_transport_ble_is_tx_ready(transport);

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            bool new_link;
            int64_t now_us = esp_timer_get_time();

            xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
            before = transport->state;
            new_link = !transport->state.connected ||
                       transport->conn_handle != event->connect.conn_handle;
            if (new_link) {
                transport->link_generation++;
            }
            transport->conn_handle = event->connect.conn_handle;
            transport->mtu = BLE_ATT_MTU_DFLT;
            transport->stale_phase_started_us = now_us;
            transport->last_rx_us = now_us;
            transport->state.connected = true;
            if (new_link) {
                transport->state.subscribed = false;
                transport->state.has_passkey = false;
                transport->state.passkey = 0;
            }
            esp_desktop_buddy_transport_ble_refresh_security_locked(transport);
            transport->state.tx_ready = esp_desktop_buddy_transport_ble_state_tx_ready(&transport->state);
            after = transport->state;
            xSemaphoreGive(transport->state_mutex);
            esp_desktop_buddy_transport_ble_emit_state_change(transport, before, after);

            ESP_LOGI(TAG, "ble connected conn_handle=%u", transport->conn_handle);
            esp_desktop_buddy_transport_ble_record_conn_params(transport,
                                                                transport->conn_handle,
                                                                false);
            if (!after.subscribed) {
                esp_desktop_buddy_transport_ble_schedule_stale_link_check(transport);
            }
            if (!after.encrypted) {
                rc = ble_gap_security_initiate(transport->conn_handle);
                if (rc != 0 && rc != BLE_HS_EALREADY) {
                    ESP_LOGW(TAG, "ble_gap_security_initiate failed rc=%d", rc);
                }
            }
        } else if (!transport->shutting_down) {
            ESP_LOGW(TAG,
                     "connection attempt failed status=%d",
                     event->connect.status);
            esp_desktop_buddy_transport_ble_start_advertising(transport);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "ble disconnected reason=%d", event->disconnect.reason);
        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        before = transport->state;
        transport->link_generation++;
        esp_desktop_buddy_transport_ble_reset_link_locked(transport);
        transport->state.tx_ready = false;
        after = transport->state;
        xSemaphoreGive(transport->state_mutex);
        esp_desktop_buddy_transport_ble_cancel_stale_link_check(transport);
        if (transport->buddy != NULL) {
            esp_desktop_buddy_transport_port_reset_session(transport->buddy);
        }
        esp_desktop_buddy_transport_ble_emit_state_change(transport, before, after);
        if (!transport->shutting_down) {
            esp_desktop_buddy_transport_ble_start_advertising(transport);
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE: {
        bool connected;

        if (!transport->shutting_down) {
            ESP_LOGI(TAG, "advertising complete reason=%d", event->adv_complete.reason);
            xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
            connected = transport->state.connected;
            xSemaphoreGive(transport->state_mutex);
            if (!connected) {
                esp_desktop_buddy_transport_ble_start_advertising(transport);
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle != transport->tx_handle) {
            return 0;
        }
        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        before = transport->state;
        transport->conn_handle = event->subscribe.conn_handle;
        transport->state.connected = true;
        esp_desktop_buddy_transport_ble_refresh_security_locked(transport);
        transport->state.subscribed = event->subscribe.cur_notify != 0;
        if (transport->state.encrypted &&
            before.subscribed != transport->state.subscribed) {
            int64_t now_us = esp_timer_get_time();

            transport->stale_phase_started_us = now_us;
            if (transport->state.subscribed) {
                transport->last_rx_us = now_us;
            }
        }
        transport->state.tx_ready = esp_desktop_buddy_transport_ble_state_tx_ready(&transport->state);
        after = transport->state;
        xSemaphoreGive(transport->state_mutex);
        if (was_tx_ready && !after.tx_ready && transport->buddy != NULL) {
            esp_desktop_buddy_transport_port_reset_session(transport->buddy);
        }
        esp_desktop_buddy_transport_ble_emit_state_change(transport, before, after);
        ESP_LOGI(TAG, "tx subscribe notify=%d", after.subscribed);
        esp_desktop_buddy_transport_ble_schedule_stale_link_check(transport);
        return 0;
    case BLE_GAP_EVENT_MTU:
        mtu = 0;
        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        if (event->mtu.conn_handle == transport->conn_handle) {
            transport->mtu = event->mtu.value;
            mtu = transport->mtu;
        }
        xSemaphoreGive(transport->state_mutex);
        if (mtu != 0) {
            ESP_LOGI(TAG, "mtu updated to %u", mtu);
        }
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (event->conn_update.status == 0) {
            xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
            if (transport->conn_param_update_pending &&
                event->conn_update.conn_handle == transport->conn_handle) {
                transport->conn_param_update_pending = false;
                if (!transport->conn_param_update_enable) {
                    transport->high_throughput_attempts = 0;
                    transport->high_throughput_retry_after_us = 0;
                }
            }
            xSemaphoreGive(transport->state_mutex);
            esp_desktop_buddy_transport_ble_record_conn_params(transport,
                                                                event->conn_update.conn_handle,
                                                                true);
        } else {
            bool retry_high_throughput = false;
            uint8_t attempts = 0;

            xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
            transport->diagnostics.conn_param_update_failures++;
            if (transport->conn_param_update_pending &&
                event->conn_update.conn_handle == transport->conn_handle) {
                retry_high_throughput = transport->conn_param_update_enable;
                attempts = transport->high_throughput_attempts;
                transport->high_throughput_requested =
                    transport->conn_param_update_previous_requested;
                transport->conn_param_update_pending = false;
                if (retry_high_throughput && attempts < 3) {
                    transport->high_throughput_retry_after_us = esp_timer_get_time() + 500000;
                } else if (!retry_high_throughput) {
                    transport->high_throughput_attempts = 0;
                    transport->high_throughput_retry_after_us = 0;
                }
            }
            xSemaphoreGive(transport->state_mutex);
            ESP_LOGW(TAG,
                     "connection parameter %s update failed status=%d attempt=%u",
                     retry_high_throughput ? "high-throughput" : "restore",
                     event->conn_update.status,
                     (unsigned)attempts);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        before = transport->state;
        transport->conn_handle = event->enc_change.conn_handle;
        transport->state.connected = true;
        esp_desktop_buddy_transport_ble_refresh_security_locked(transport);
        if (event->enc_change.status == 0) {
            int64_t now_us = esp_timer_get_time();

            transport->state.encrypted = true;
            transport->state.has_passkey = false;
            transport->state.passkey = 0;
            transport->stale_phase_started_us = now_us;
            if (transport->state.subscribed) {
                transport->last_rx_us = now_us;
            }
        }
        transport->state.tx_ready = esp_desktop_buddy_transport_ble_state_tx_ready(&transport->state);
        after = transport->state;
        xSemaphoreGive(transport->state_mutex);
        if (was_tx_ready && !after.tx_ready && transport->buddy != NULL) {
            esp_desktop_buddy_transport_port_reset_session(transport->buddy);
        }
        esp_desktop_buddy_transport_ble_emit_state_change(transport, before, after);
        ESP_LOGI(TAG,
                 "encryption change status=%d encrypted=%d bonded=%d",
                 event->enc_change.status,
                 after.encrypted,
                 after.bonded);
        if (event->enc_change.status == 0) {
            esp_desktop_buddy_transport_ble_schedule_stale_link_check(transport);
        }
        if (event->enc_change.status != 0 && !transport->shutting_down) {
            rc = ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            if (rc != 0 && rc != BLE_HS_ENOTCONN) {
                ESP_LOGW(TAG,
                         "ble_gap_terminate failed rc=%d after enc_change status=%d",
                         rc,
                         event->enc_change.status);
            }
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            rc = ble_store_util_delete_peer(&desc.peer_id_addr);
            if (rc != 0) {
                ESP_LOGW(TAG, "ble_store_util_delete_peer failed rc=%d", rc);
            } else {
                ESP_LOGI(TAG, "deleted old bond for repeat pairing");
            }
        } else {
            ESP_LOGW(TAG, "ble_gap_conn_find failed rc=%d during repeat pairing", rc);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = {0};

        if (event->passkey.params.action != BLE_SM_IOACT_DISP) {
            ESP_LOGW(TAG, "unsupported passkey action=%d", event->passkey.params.action);
            return 0;
        }

        pkey.action = event->passkey.params.action;
        pkey.passkey = esp_random() % 1000000;
        rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        if (rc != 0) {
            ESP_LOGW(TAG, "ble_sm_inject_io failed rc=%d", rc);
            return 0;
        }

        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        before = transport->state;
        transport->state.has_passkey = true;
        transport->state.passkey = pkey.passkey;
        transport->state.tx_ready = esp_desktop_buddy_transport_ble_state_tx_ready(&transport->state);
        after = transport->state;
        xSemaphoreGive(transport->state_mutex);
        esp_desktop_buddy_transport_ble_emit_state_change(transport, before, after);
        /* 配对码只交给屏幕，日志不可保存可用于配对的秘密。 */
        ESP_LOGI(TAG, "pairing code available on device display");
        return 0;
    }
    default:
        return 0;
    }
}

void esp_desktop_buddy_transport_ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "nimble reset reason=%d", reason);
}

void esp_desktop_buddy_transport_ble_on_sync(void)
{
    uint8_t addr_val[6] = {0};
    int rc;

    if (g_esp_desktop_buddy_transport_ble_active == NULL) {
        return;
    }

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &g_esp_desktop_buddy_transport_ble_active->own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(g_esp_desktop_buddy_transport_ble_active->own_addr_type, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_copy_addr failed rc=%d", rc);
        return;
    }

    rc = ble_svc_gap_device_name_set(g_esp_desktop_buddy_transport_ble_active->advertising_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG,
             "ble sync complete addr=%02x:%02x:%02x:%02x:%02x:%02x",
             addr_val[0],
             addr_val[1],
             addr_val[2],
             addr_val[3],
             addr_val[4],
             addr_val[5]);
    esp_desktop_buddy_transport_ble_start_advertising(g_esp_desktop_buddy_transport_ble_active);
}

void esp_desktop_buddy_transport_ble_host_task(void *param)
{
    esp_desktop_buddy_transport_ble_t *transport = g_esp_desktop_buddy_transport_ble_active;

    (void)param;
    if (transport != NULL && transport->state_mutex != NULL) {
        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        transport->host_task = xTaskGetCurrentTaskHandle();
        xSemaphoreGive(transport->state_mutex);
    }
    nimble_port_run();
    if (transport != NULL && transport->state_mutex != NULL) {
        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        transport->host_task = NULL;
        xSemaphoreGive(transport->state_mutex);
    }
    if (transport != NULL && transport->host_stop_sem != NULL) {
        xSemaphoreGive(transport->host_stop_sem);
    }
    vTaskSuspend(NULL);
}
