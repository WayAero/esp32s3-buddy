/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "permission.h"
#include "command_handlers.h"
#include "events.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Public configuration used to create a Buddy protocol core instance. */
typedef struct {
    /** Sink that receives typed protocol events. */
    esp_desktop_buddy_event_listener_t event_sink;
    /** Application-owned built-in command handlers and optional command extensions. */
    esp_desktop_buddy_command_handlers_t handlers;
} esp_desktop_buddy_config_t;

/** Cumulative ingress statistics for the shared core inbox. */
typedef struct {
    uint32_t chunks_received;
    uint32_t bytes_received;
    uint32_t enqueue_failures;
    uint32_t queue_high_water;
    uint32_t max_chunk;
    uint32_t queue_wait_count;
    uint64_t queue_wait_total_us;
    uint32_t queue_wait_max_us;
    uint32_t folder_chunk_handler_count;
    uint64_t folder_chunk_handler_total_us;
    uint32_t folder_chunk_handler_max_us;
    uint32_t folder_chunk_ack_encode_count;
    uint64_t folder_chunk_ack_encode_total_us;
    uint32_t folder_chunk_ack_encode_max_us;
} esp_desktop_buddy_rx_diagnostics_t;

/**
 * @brief Creates a Buddy core with the supplied event sink and command bindings.
 *
 * @param config Optional configuration. Pass NULL for a zero-initialized core.
 * @param[out] out_buddy Returned Buddy core instance.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if @p out_buddy is NULL
 *      - ESP_ERR_NO_MEM if allocation or task creation fails
 */
esp_err_t esp_desktop_buddy_new(const esp_desktop_buddy_config_t *config, esp_desktop_buddy_t **out_buddy);
/**
 * @brief Destroys a Buddy core after transports have detached.
 *
 * When called from a Buddy callback, destruction is deferred until the callback
 * returns. In that case, ESP_OK means deletion has been scheduled, not that all
 * resources have already been released.
 *
 * @param buddy Buddy core instance to destroy.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if @p buddy is NULL
 *      - ESP_ERR_INVALID_STATE if a transport is still attached
 */
esp_err_t esp_desktop_buddy_delete(esp_desktop_buddy_t *buddy);

/**
 * @brief Feeds one transport-provided RX byte span into the core parser.
 *
 * @param buddy Buddy core instance that owns the parser state.
 * @param data RX bytes to consume.
 * @param len Number of bytes in @p data.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_INVALID_SIZE if @p len exceeds the configured per-call RX cap
 *      - ESP_ERR_NO_MEM if the non-blocking core inbox is full
 */
esp_err_t esp_desktop_buddy_receive_bytes(esp_desktop_buddy_t *buddy, const uint8_t *data, size_t len);
/**
 * @brief Copies cumulative RX inbox statistics.
 *
 * `queue_high_water` includes both received chunks and locally posted actions
 * because both use the same inbox queue.
 */
esp_err_t esp_desktop_buddy_get_rx_diagnostics(esp_desktop_buddy_t *buddy,
                                                esp_desktop_buddy_rx_diagnostics_t *out_diagnostics);
/**
 * @brief Posts an application-local action such as reply once or deny.
 *
 * @param buddy Buddy core instance that owns the action queue.
 * @param action Action to enqueue.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_NO_MEM if the non-blocking action queue is full
 */
esp_err_t esp_desktop_buddy_post_permission_reply(esp_desktop_buddy_t *buddy, const esp_desktop_buddy_permission_reply_t *action);
/**
 * @brief Queues a one-time approval response for the specified prompt id.
 *
 * @param buddy Buddy core instance that owns the action queue.
 * @param prompt_id Active prompt identifier to approve once.
 *
 * @return See esp_desktop_buddy_post_permission_reply().
 */
esp_err_t esp_desktop_buddy_prompt_approve_once(esp_desktop_buddy_t *buddy, const char *prompt_id);
/**
 * @brief Queues a deny response for the specified prompt id.
 *
 * @param buddy Buddy core instance that owns the action queue.
 * @param prompt_id Active prompt identifier to deny.
 *
 * @return See esp_desktop_buddy_post_permission_reply().
 */
esp_err_t esp_desktop_buddy_prompt_deny(esp_desktop_buddy_t *buddy, const char *prompt_id);

#ifdef __cplusplus
}
#endif
