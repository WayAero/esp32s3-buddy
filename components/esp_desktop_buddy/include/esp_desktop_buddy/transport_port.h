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
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_desktop_buddy esp_desktop_buddy_t;

/** Metadata for one encoded frame dequeued by a transport. */
typedef struct {
    bool is_folder_chunk_ack;
    int64_t enqueued_us;
} esp_desktop_buddy_transport_frame_metadata_t;

/**
 * @brief Marks a transport as attached so RX/TX state can be used safely.
 *
 * @param buddy Buddy core instance to mark as transport-owned.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if @p buddy is NULL
 *      - ESP_ERR_INVALID_STATE if another transport is already attached
 */
esp_err_t esp_desktop_buddy_transport_port_attach(esp_desktop_buddy_t *buddy);
/**
 * @brief Detaches the active transport during transport teardown.
 *
 * @param buddy Buddy core instance to detach from the active transport.
 */
void esp_desktop_buddy_transport_port_detach(esp_desktop_buddy_t *buddy);

/**
 * @brief Dequeues one encoded outbound frame for a transport to transmit.
 *
 * @param buddy Buddy core instance that owns the TX queue.
 * @param[out] buf Destination buffer for the encoded frame.
 * @param buf_size Size of @p buf in bytes.
 * @param[out] out_len Encoded byte count written into @p buf.
 * @param[out] out_metadata Optional metadata associated with the dequeued frame.
 * @param timeout Queue wait timeout in FreeRTOS ticks.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_TIMEOUT if no frame becomes available before @p timeout
 */
esp_err_t esp_desktop_buddy_transport_port_next_frame(esp_desktop_buddy_t *buddy,
                           uint8_t *buf,
                           size_t buf_size,
                           size_t *out_len,
                           esp_desktop_buddy_transport_frame_metadata_t *out_metadata,
                           TickType_t timeout);
/**
 * @brief Drops queued outbound frames for the current session.
 *
 * @param buddy Buddy core instance that owns the TX queue.
 */
void esp_desktop_buddy_transport_port_drop_frames(esp_desktop_buddy_t *buddy);

/**
 * @brief Clears transport-session state after a confirmed link loss.
 *
 * Drops outbound frames and invalidates the mirrored snapshot so a reconnecting
 * peer can establish a fresh prompt lifecycle, including reuse of the same
 * prompt id when the previous reply may have been lost with the link.
 *
 * @param buddy Buddy core instance whose transport session ended.
 */
void esp_desktop_buddy_transport_port_reset_session(esp_desktop_buddy_t *buddy);

#ifdef __cplusplus
}
#endif
