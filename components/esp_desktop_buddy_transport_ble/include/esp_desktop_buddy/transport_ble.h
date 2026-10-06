/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_desktop_buddy esp_desktop_buddy_t;
typedef struct esp_desktop_buddy_transport_ble esp_desktop_buddy_transport_ble_t;

/* Legacy scan response: 31 bytes minus the name AD length/type bytes. */
#define ESP_DESKTOP_BUDDY_TRANSPORT_BLE_NAME_MAX 29
void esp_desktop_buddy_transport_ble_build_default_name(char *out, size_t out_size);

/** BLE IO capabilities exposed by the Buddy BLE transport. */
typedef enum {
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_IO_CAP_DISPLAY_ONLY = 0,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_IO_CAP_NO_INPUT_OUTPUT,
} esp_desktop_buddy_transport_ble_io_cap_t;

/** Security policy used when bringing up the Buddy BLE transport. */
typedef struct {
    bool bonding;
    bool mitm;
    bool secure_connections;
    esp_desktop_buddy_transport_ble_io_cap_t io_capability;
} esp_desktop_buddy_transport_ble_security_config_t;

/** Observable connection and security state for the active BLE session. */
typedef struct {
    bool connected;
    bool subscribed;
    bool encrypted;
    bool bonded;
    bool tx_ready;
    bool has_passkey;
    uint32_t passkey;
} esp_desktop_buddy_transport_ble_state_t;

/** Cumulative time spent submitting folder chunk ACK notifications to NimBLE. */
typedef struct {
    bool conn_params_valid;
    uint16_t conn_interval_units;
    uint16_t slave_latency;
    uint16_t supervision_timeout_units;
    uint32_t conn_param_update_count;
    uint32_t conn_param_update_failures;
    uint32_t chunk_ack_tx_queue_count;
    uint64_t chunk_ack_tx_queue_total_us;
    uint32_t chunk_ack_tx_queue_max_us;
    uint32_t chunk_ack_notify_count;
    uint64_t chunk_ack_notify_total_us;
    uint32_t chunk_ack_notify_max_us;
} esp_desktop_buddy_transport_ble_diagnostics_t;

/** Bitmask describing which transport state fields changed in an event. */
typedef enum {
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_FIELD_CONNECTED = 1u << 0,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_FIELD_SUBSCRIBED = 1u << 1,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_FIELD_ENCRYPTED = 1u << 2,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_FIELD_BONDED = 1u << 3,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_FIELD_TX_READY = 1u << 4,
    ESP_DESKTOP_BUDDY_TRANSPORT_BLE_FIELD_PASSKEY = 1u << 5,
} esp_desktop_buddy_transport_ble_field_mask_t;

/** Transport event payload delivered to the application callback. */
typedef struct {
    uint32_t changed_fields;
    esp_desktop_buddy_transport_ble_state_t state;
} esp_desktop_buddy_transport_ble_event_t;

/**
 * @brief Event callback invoked for Buddy BLE transport state changes.
 *
 * Callbacks run on the transport/NimBLE host task. Keep them short and hand
 * off teardown or other blocking work to your app task instead of calling
 * esp_desktop_buddy_transport_ble_delete() inline from the callback.
 */
typedef void (*esp_desktop_buddy_transport_ble_event_handler_t)(void *ctx,
                                               const esp_desktop_buddy_transport_ble_event_t *event);

/** Configuration used to create a Buddy BLE transport instance. */
typedef struct {
    esp_desktop_buddy_t *buddy;
    /*
     * Optional BLE advertising-name override, at most 29 UTF-8 bytes.
     *
     * Default is DeepSeek-XXXX. Desktop clients that filter by a fixed prefix
     * must support this default and user-defined names, or discover by service UUID.
     */
    const char *advertising_name_override;
    esp_desktop_buddy_transport_ble_security_config_t security;
    esp_desktop_buddy_transport_ble_event_handler_t on_event;
    void *event_ctx;
} esp_desktop_buddy_transport_ble_config_t;

/**
 * @brief Creates a Buddy BLE transport and attaches it to the supplied core.
 *
 * The transport supports a single active BLE peer at a time. Once connected,
 * it stops advertising until that link disconnects.
 *
 * @param config Transport configuration to apply.
 * @param[out] out_transport Returned Buddy BLE transport instance.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the configuration is invalid
 *      - ESP_ERR_INVALID_STATE if another transport instance is already active
 *      - ESP_ERR_NO_MEM if allocation or task creation fails
 *      - ESP_ERR_NOT_SUPPORTED if the requested IO capability is unsupported
 */
esp_err_t esp_desktop_buddy_transport_ble_new(const esp_desktop_buddy_transport_ble_config_t *config,
                                  esp_desktop_buddy_transport_ble_t **out_transport);
/**
 * @brief Tears down the BLE transport and detaches it from the core.
 *
 * @param transport Transport instance to destroy.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if @p transport is NULL
 *      - ESP_ERR_TIMEOUT if the internal tasks do not stop before teardown deadlines
 */
esp_err_t esp_desktop_buddy_transport_ble_delete(esp_desktop_buddy_transport_ble_t *transport);
/**
 * @brief Clears bonded peers known to the transport.
 *
 * @param transport Transport instance to clear.
 *
 * @return
 *      - ESP_OK when the local bond store is cleared successfully
 *      - ESP_ERR_INVALID_ARG if @p transport is NULL
 *      - ESP_FAIL if local bond storage clearing fails
 *
 * Active links are terminated on a best-effort basis after the local bond
 * store is cleared. Disconnect failures are logged but do not change the
 * return value because the bond clear itself already succeeded.
 */
esp_err_t esp_desktop_buddy_transport_ble_clear_bonds(esp_desktop_buddy_transport_ble_t *transport);
/**
 * @brief Copies the current transport state into application-owned storage.
 *
 * @param transport Transport instance to query.
 * @param[out] out_state Destination buffer for the copied state.
 *
 * @return ESP_OK on success, or ESP_ERR_INVALID_ARG if the arguments are invalid.
 */
esp_err_t esp_desktop_buddy_transport_ble_get_state(esp_desktop_buddy_transport_ble_t *transport,
                                        esp_desktop_buddy_transport_ble_state_t *out_state);
esp_err_t esp_desktop_buddy_transport_ble_get_diagnostics(
    esp_desktop_buddy_transport_ble_t *transport,
    esp_desktop_buddy_transport_ble_diagnostics_t *out_diagnostics);
/**
 * @brief Requests a 15 ms connection interval for active role-package transfer.
 *
 * Disabling restores the interval, latency and supervision timeout negotiated
 * before the transfer. A peer can reject either request; such rejection does
 * not interrupt the transport session.
 */
esp_err_t esp_desktop_buddy_transport_ble_set_high_throughput(
    esp_desktop_buddy_transport_ble_t *transport,
    bool enable);
/**
 * @brief Copies the configured BLE advertising name into application-owned storage.
 *
 * @param transport Transport instance to query.
 * @param[out] out_name Destination buffer for the copied name.
 * @param out_name_size Size of @p out_name in bytes.
 *
 * @return ESP_OK on success, or ESP_ERR_INVALID_ARG if the arguments are invalid.
 */
esp_err_t esp_desktop_buddy_transport_ble_get_advertising_name(esp_desktop_buddy_transport_ble_t *transport,
                                                    char *out_name,
                                                    size_t out_name_size);
/**
 * @brief Returns whether the active connection can send encrypted Buddy traffic.
 *
 * @param transport Transport instance to query.
 *
 * @return true when encrypted TX is ready, otherwise false.
 */
bool esp_desktop_buddy_transport_ble_is_tx_ready(esp_desktop_buddy_transport_ble_t *transport);

#ifdef __cplusplus
}
#endif
