/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_desktop_buddy esp_desktop_buddy_t;

/**
 * @brief Returns the current public scalar state mirrored by the Buddy core.
 *
 * @param buddy Buddy core instance to query.
 * @param[out] out_info Destination structure for the mirrored scalar state.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 */
esp_err_t esp_desktop_buddy_get_snapshot_info(esp_desktop_buddy_t *buddy, esp_desktop_buddy_snapshot_info_t *out_info);
/**
 * @brief Copies the current message into caller-owned storage.
 *
 * @param buddy Buddy core instance to query.
 * @param[out] buf Destination buffer for the NUL-terminated message string.
 * @param buf_size Size of @p buf in bytes.
 * @param[out] out_required_size Optional required byte count including the
 *             terminating NUL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_NOT_FOUND if no state has been received yet
 *      - ESP_ERR_INVALID_SIZE if @p buf_size is too small
 */
esp_err_t esp_desktop_buddy_get_message(esp_desktop_buddy_t *buddy,
                            char *buf,
                            size_t buf_size,
                            size_t *out_required_size);
/**
 * @brief Returns the number of mirrored entries in the current state.
 *
 * @param buddy Buddy core instance to query.
 * @param[out] out_count Destination entry count.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_NOT_FOUND if no state has been received yet
 */
esp_err_t esp_desktop_buddy_get_entry_count(esp_desktop_buddy_t *buddy, size_t *out_count);
/**
 * @brief Copies one mirrored entry into caller-owned storage.
 *
 * @param buddy Buddy core instance to query.
 * @param index Zero-based entry index to copy.
 * @param[out] buf Destination buffer for the NUL-terminated entry text.
 * @param buf_size Size of @p buf in bytes.
 * @param[out] out_required_size Optional required byte count including the
 *             terminating NUL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_NOT_FOUND if no state is present or @p index is out of range
 *      - ESP_ERR_INVALID_SIZE if @p buf_size is too small
 */
esp_err_t esp_desktop_buddy_get_entry(esp_desktop_buddy_t *buddy,
                          size_t index,
                          char *buf,
                          size_t buf_size,
                          size_t *out_required_size);
/**
 * @brief Copies the active prompt identifier into caller-owned storage.
 *
 * @param buddy Buddy core instance to query.
 * @param[out] buf Destination buffer for the NUL-terminated prompt id.
 * @param buf_size Size of @p buf in bytes.
 * @param[out] out_required_size Optional required byte count including the
 *             terminating NUL.
 *
 * @return See esp_desktop_buddy_get_message().
 */
esp_err_t esp_desktop_buddy_get_prompt_id(esp_desktop_buddy_t *buddy,
                              char *buf,
                              size_t buf_size,
                              size_t *out_required_size);
/**
 * @brief Copies the active prompt tool into caller-owned storage.
 *
 * @param buddy Buddy core instance to query.
 * @param[out] buf Destination buffer for the NUL-terminated prompt tool.
 * @param buf_size Size of @p buf in bytes.
 * @param[out] out_required_size Optional required byte count including the
 *             terminating NUL.
 *
 * @return See esp_desktop_buddy_get_message().
 */
esp_err_t esp_desktop_buddy_get_prompt_tool(esp_desktop_buddy_t *buddy,
                                char *buf,
                                size_t buf_size,
                                size_t *out_required_size);
/**
 * @brief Copies the active prompt hint into caller-owned storage.
 *
 * @param buddy Buddy core instance to query.
 * @param[out] buf Destination buffer for the NUL-terminated prompt hint.
 * @param buf_size Size of @p buf in bytes.
 * @param[out] out_required_size Optional required byte count including the
 *             terminating NUL.
 *
 * @return See esp_desktop_buddy_get_message().
 */
esp_err_t esp_desktop_buddy_get_prompt_hint(esp_desktop_buddy_t *buddy,
                                char *buf,
                                size_t buf_size,
                                size_t *out_required_size);
/**
 * @brief Returns whether the active Buddy state is still live.
 *
 * @param buddy Buddy core instance to query.
 *
 * @return true if the current state is live, otherwise false.
 */
bool esp_desktop_buddy_is_live(esp_desktop_buddy_t *buddy);

#ifdef __cplusplus
}
#endif
