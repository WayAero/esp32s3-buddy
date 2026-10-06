/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#include "esp_desktop_buddy/core.h"
#include "esp_desktop_buddy/transport_port.h"
#include "esp_desktop_buddy/transport_ble.h"

#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_TX_TASK_STACK 4096
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_TX_TASK_PRIORITY 5
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_TX_TASK_CORE_ID 0
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_RX_WRITE_MAX CONFIG_ESP_DESKTOP_BUDDY_FEED_RX_CHUNK_MAX
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_HIGH_THROUGHPUT_INTERVAL_UNITS 12
struct esp_desktop_buddy_transport_ble {
    esp_desktop_buddy_t *buddy;
    bool buddy_attached;
    esp_desktop_buddy_transport_ble_event_handler_t on_event;
    void *event_ctx;
    SemaphoreHandle_t state_mutex;
    SemaphoreHandle_t host_stop_sem;
    SemaphoreHandle_t tx_stop_sem;
    TaskHandle_t tx_task;
    TaskHandle_t host_task;
    char advertising_name[ESP_DESKTOP_BUDDY_TRANSPORT_BLE_NAME_MAX + 1];
    esp_desktop_buddy_transport_ble_security_config_t security;
    esp_desktop_buddy_transport_ble_state_t state;
    esp_desktop_buddy_transport_ble_diagnostics_t diagnostics;
    bool high_throughput_requested;
    bool conn_param_update_pending;
    bool conn_param_update_enable;
    bool conn_param_update_previous_requested;
    uint8_t high_throughput_attempts;
    int64_t high_throughput_retry_after_us;
    bool restore_conn_params_valid;
    uint16_t restore_conn_interval_units;
    uint16_t restore_slave_latency;
    uint16_t restore_supervision_timeout_units;
    uint16_t conn_handle;
    uint16_t tx_handle;
    uint16_t mtu;
    uint8_t own_addr_type;
    struct ble_npl_callout stale_link_callout;
    bool stale_link_callout_initialized;
    bool delete_in_progress;
    bool host_stop_requested;
    uint64_t link_generation;
    int64_t stale_phase_started_us;
    int64_t last_rx_us;
    volatile bool shutting_down;
    struct ble_gatt_chr_def gatt_chars[3];
    struct ble_gatt_svc_def gatt_svcs[2];
};

extern esp_desktop_buddy_transport_ble_t *g_esp_desktop_buddy_transport_ble_active;

extern void ble_store_config_init(void);

bool esp_desktop_buddy_transport_ble_state_tx_ready(const esp_desktop_buddy_transport_ble_state_t *state);
void esp_desktop_buddy_transport_ble_emit_state_change(esp_desktop_buddy_transport_ble_t *transport,
                                           esp_desktop_buddy_transport_ble_state_t before,
                                           esp_desktop_buddy_transport_ble_state_t after);
void esp_desktop_buddy_transport_ble_reset_link_locked(esp_desktop_buddy_transport_ble_t *transport);
void esp_desktop_buddy_transport_ble_refresh_security_locked(esp_desktop_buddy_transport_ble_t *transport);
void esp_desktop_buddy_transport_ble_force_link_recovery(esp_desktop_buddy_transport_ble_t *transport);
esp_err_t esp_desktop_buddy_transport_ble_init_stale_link_callout(esp_desktop_buddy_transport_ble_t *transport);
void esp_desktop_buddy_transport_ble_deinit_stale_link_callout(esp_desktop_buddy_transport_ble_t *transport);
void esp_desktop_buddy_transport_ble_schedule_stale_link_check(esp_desktop_buddy_transport_ble_t *transport);
void esp_desktop_buddy_transport_ble_cancel_stale_link_check(esp_desktop_buddy_transport_ble_t *transport);
uint16_t esp_desktop_buddy_transport_ble_get_mtu(esp_desktop_buddy_transport_ble_t *transport);
esp_err_t esp_desktop_buddy_transport_ble_wait_for_stop(SemaphoreHandle_t stop_sem,
                                            TickType_t timeout,
                                            const char *task_name);
void esp_desktop_buddy_transport_ble_delete_unstarted(esp_desktop_buddy_transport_ble_t *transport,
                                          bool nimble_initialized);
void esp_desktop_buddy_transport_ble_tx_task(void *arg);
void esp_desktop_buddy_transport_ble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void esp_desktop_buddy_transport_ble_on_reset(int reason);
void esp_desktop_buddy_transport_ble_on_sync(void);
void esp_desktop_buddy_transport_ble_host_task(void *param);
void esp_desktop_buddy_transport_ble_prepare_gatt(esp_desktop_buddy_transport_ble_t *transport);
