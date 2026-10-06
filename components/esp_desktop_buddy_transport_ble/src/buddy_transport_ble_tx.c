/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "buddy_transport_ble_internal.h"

static const char *TAG = "esp_desktop_buddy_transport_ble";

/* ATT notifications consume 1 byte opcode + 2 byte attribute handle. */
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_ATT_NOTIFY_OVERHEAD 3
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_DEFAULT_NOTIFY_CHUNK_LEN \
    (BLE_ATT_MTU_DFLT - ESP_DESKTOP_BUDDY_TRANSPORT_BLE_ATT_NOTIFY_OVERHEAD)

static bool esp_desktop_buddy_transport_ble_is_folder_chunk_ack(const uint8_t *data, size_t len)
{
    static const char prefix[] = "{\"ack\":\"chunk\"";

    return data != NULL && len >= sizeof(prefix) - 1 &&
           memcmp(data, prefix, sizeof(prefix) - 1) == 0;
}

static void esp_desktop_buddy_transport_ble_delay_after_chunk(void)
{
    TickType_t ticks;

    if (CONFIG_ESP_DESKTOP_BUDDY_TRANSPORT_BLE_NOTIFY_CHUNK_DELAY_MS <= 0) {
        return;
    }

    ticks = pdMS_TO_TICKS(CONFIG_ESP_DESKTOP_BUDDY_TRANSPORT_BLE_NOTIFY_CHUNK_DELAY_MS);
    if (ticks == 0) {
        ticks = 1;
    }
    vTaskDelay(ticks);
}

static esp_err_t esp_desktop_buddy_transport_ble_send_bytes(esp_desktop_buddy_transport_ble_t *transport,
                                                const uint8_t *data,
                                                size_t len)
{
    size_t chunk_len = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_DEFAULT_NOTIFY_CHUNK_LEN;
    esp_desktop_buddy_transport_ble_state_t state;
    uint16_t mtu;
    const bool is_folder_chunk_ack = esp_desktop_buddy_transport_ble_is_folder_chunk_ack(data, len);
    const int64_t notify_started_us = is_folder_chunk_ack ? esp_timer_get_time() : 0;

    if (transport == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_desktop_buddy_transport_ble_get_state(transport, &state) != ESP_OK || !state.tx_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    mtu = esp_desktop_buddy_transport_ble_get_mtu(transport);
    if (mtu > ESP_DESKTOP_BUDDY_TRANSPORT_BLE_ATT_NOTIFY_OVERHEAD) {
        chunk_len = mtu - ESP_DESKTOP_BUDDY_TRANSPORT_BLE_ATT_NOTIFY_OVERHEAD;
    }
    if (chunk_len > CONFIG_ESP_DESKTOP_BUDDY_TRANSPORT_BLE_NOTIFY_CHUNK_CAP) {
        chunk_len = CONFIG_ESP_DESKTOP_BUDDY_TRANSPORT_BLE_NOTIFY_CHUNK_CAP;
    }

    for (size_t offset = 0; offset < len; offset += chunk_len) {
        size_t part_len = len - offset;
        struct os_mbuf *om;
        int rc;

        if (part_len > chunk_len) {
            part_len = chunk_len;
        }

        om = ble_hs_mbuf_from_flat(data + offset, (uint16_t)part_len);
        if (om == NULL) {
            return ESP_ERR_NO_MEM;
        }

        rc = ble_gatts_notify_custom(transport->conn_handle, transport->tx_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "notify failed rc=%d after %u bytes", rc, (unsigned)offset);
            esp_desktop_buddy_transport_ble_force_link_recovery(transport);
            return ESP_FAIL;
        }
        esp_desktop_buddy_transport_ble_delay_after_chunk();
    }

    if (is_folder_chunk_ack) {
        const uint32_t notify_elapsed_us = (uint32_t)(esp_timer_get_time() - notify_started_us);

        xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
        transport->diagnostics.chunk_ack_notify_count++;
        transport->diagnostics.chunk_ack_notify_total_us += notify_elapsed_us;
        if (notify_elapsed_us > transport->diagnostics.chunk_ack_notify_max_us) {
            transport->diagnostics.chunk_ack_notify_max_us = notify_elapsed_us;
        }
        xSemaphoreGive(transport->state_mutex);
    }
    return ESP_OK;
}

void esp_desktop_buddy_transport_ble_tx_task(void *arg)
{
    esp_desktop_buddy_transport_ble_t *transport = (esp_desktop_buddy_transport_ble_t *)arg;
    uint8_t *frame = NULL;

    frame = malloc(ESP_DESKTOP_BUDDY_FRAME_MAX);
    if (frame == NULL) {
        ESP_LOGE(TAG, "failed to allocate TX frame buffer");
        xSemaphoreGive(transport->tx_stop_sem);
        vTaskDelete(NULL);
        return;
    }

    while (!transport->shutting_down) {
        size_t len = 0;
        esp_desktop_buddy_transport_frame_metadata_t metadata = {0};
        esp_err_t err;

        if (!esp_desktop_buddy_transport_ble_is_tx_ready(transport)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        err = esp_desktop_buddy_transport_port_next_frame(transport->buddy,
                               frame,
                               ESP_DESKTOP_BUDDY_FRAME_MAX,
                               &len,
                               &metadata,
                               pdMS_TO_TICKS(100));
        if (transport->shutting_down) {
            break;
        }
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tx dequeue failed: %s", esp_err_to_name(err));
            continue;
        }

        if (metadata.is_folder_chunk_ack && metadata.enqueued_us > 0) {
            const uint32_t tx_queue_elapsed_us = (uint32_t)(esp_timer_get_time() - metadata.enqueued_us);

            xSemaphoreTake(transport->state_mutex, portMAX_DELAY);
            transport->diagnostics.chunk_ack_tx_queue_count++;
            transport->diagnostics.chunk_ack_tx_queue_total_us += tx_queue_elapsed_us;
            if (tx_queue_elapsed_us > transport->diagnostics.chunk_ack_tx_queue_max_us) {
                transport->diagnostics.chunk_ack_tx_queue_max_us = tx_queue_elapsed_us;
            }
            xSemaphoreGive(transport->state_mutex);
        }

        if (!esp_desktop_buddy_transport_ble_is_tx_ready(transport) ||
            esp_desktop_buddy_transport_ble_send_bytes(transport, frame, len) != ESP_OK) {
            esp_desktop_buddy_transport_port_drop_frames(transport->buddy);
        }
    }

    free(frame);
    xSemaphoreGive(transport->tx_stop_sem);
    vTaskDelete(NULL);
}
