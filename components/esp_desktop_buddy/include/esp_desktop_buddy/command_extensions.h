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
typedef struct esp_desktop_buddy_command_view esp_desktop_buddy_command_view_t;

/** Public ack payload emitted for handled optional commands. */
typedef struct {
    bool ok;
    uint32_t n;
    const char *error;
} esp_desktop_buddy_command_ack_t;

/** Routing policy used by optional command handlers. */
typedef enum {
    ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK = 0,
    ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_NO_ACK,
} esp_desktop_buddy_command_extension_mode_t;

/** Routed command result returned by optional command handlers. */
typedef struct {
    esp_desktop_buddy_command_extension_mode_t mode;
    esp_desktop_buddy_command_ack_t reply;
} esp_desktop_buddy_command_extension_result_t;

/**
 * @brief Handles one optional command routed by the Buddy core.
 *
 * Runs synchronously on the Buddy core task. Return
 * `ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK` when the core should emit the supplied public
 * ack payload, or `ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_NO_ACK` when the command was
 * intentionally consumed without emitting any ack.
 */
typedef esp_desktop_buddy_command_extension_result_t (*esp_desktop_buddy_command_extension_handler_t)(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request);

/** One optional command binding exposed through the core command-extension interface. */
typedef struct {
    const char *command;
    esp_desktop_buddy_command_extension_handler_t handler;
} esp_desktop_buddy_command_extension_entry_t;

/** Optional command family installed into the core. */
typedef struct {
    void *ctx;
    const esp_desktop_buddy_command_extension_entry_t *bindings;
    size_t binding_count;
} esp_desktop_buddy_command_extension_set_t;

/**
 * @brief Returns the public command name for an optional command request.
 *
 * @param request Opaque command request view.
 *
 * @return Borrowed command name, or NULL if @p request is invalid.
 */
const char *esp_desktop_buddy_command_view_name(const esp_desktop_buddy_command_view_t *request);
/**
 * @brief Copies one string field view out of an optional command request.
 *
 * @param request Opaque command request view.
 * @param key Object field name to read.
 * @param[out] out_value Borrowed string value when present.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_NOT_FOUND if the field is missing or not a string
 */
esp_err_t esp_desktop_buddy_command_view_get_string(const esp_desktop_buddy_command_view_t *request,
                                           const char *key,
                                           const char **out_value);
/**
 * @brief Copies one clamped unsigned integer field out of an optional command request.
 *
 * JSON number values clamp to the inclusive range `[0, UINT32_MAX]` and
 * truncate fractional input toward zero.
 *
 * @param request Opaque command request view.
 * @param key Object field name to read.
 * @param[out] out_value Parsed numeric value when present.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the arguments are invalid
 *      - ESP_ERR_NOT_FOUND if the field is missing or not numeric
 */
esp_err_t esp_desktop_buddy_command_view_get_u32(const esp_desktop_buddy_command_view_t *request,
                                        const char *key,
                                        uint32_t *out_value);

/**
 * @brief Reads one JSON field as an exact unsigned 32-bit integer.
 *
 * Rejects negative, fractional, and out-of-range JSON numbers. Use this for
 * wire-protocol fields whose byte or version semantics must not be changed by
 * numeric coercion.
 */
esp_err_t esp_desktop_buddy_command_view_get_u32_strict(const esp_desktop_buddy_command_view_t *request,
                                                        const char *key,
                                                        uint32_t *out_value);

#ifdef __cplusplus
}
#endif
