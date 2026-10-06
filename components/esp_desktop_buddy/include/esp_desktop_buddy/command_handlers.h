/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "command_extensions.h"
#include "types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Result for non-status commands. */
typedef struct {
    esp_err_t err;
    const char *detail;
} esp_desktop_buddy_command_result_t;

/** Result for status handlers, including a serialized JSON object view. */
typedef struct {
    esp_desktop_buddy_command_result_t result;
    esp_desktop_buddy_json_object_view_t data;
} esp_desktop_buddy_status_reply_t;

/**
 * @brief Builds the public status response payload for the current device state.
 *
 * Runs synchronously on the Buddy core task. The returned JSON view must point
 * at a serialized JSON object that remains valid until the handler returns and
 * the core consumes it.
 */
typedef esp_desktop_buddy_status_reply_t (*esp_desktop_buddy_status_handler_t)(void *ctx, esp_desktop_buddy_t *buddy);
/**
 * @brief Handles the public name command.
 *
 * Runs synchronously on the Buddy core task. Keep handler work short and avoid
 * blocking on long-lived storage or transport operations when possible.
 */
typedef esp_desktop_buddy_command_result_t (*esp_desktop_buddy_name_handler_t)(void *ctx, esp_desktop_buddy_t *buddy, const char *name);
/**
 * @brief Handles the public owner command.
 *
 * Runs synchronously on the Buddy core task. Keep handler work short and avoid
 * blocking on long-lived storage or transport operations when possible.
 */
typedef esp_desktop_buddy_command_result_t (*esp_desktop_buddy_owner_handler_t)(void *ctx, esp_desktop_buddy_t *buddy, const char *name);
/**
 * @brief Handles the public unpair command.
 *
 * Runs synchronously on the Buddy core task. Keep handler work short and avoid
 * blocking on long-lived storage or transport operations when possible.
 */
typedef esp_desktop_buddy_command_result_t (*esp_desktop_buddy_unpair_handler_t)(void *ctx, esp_desktop_buddy_t *buddy);
/** Application-owned command routing bindings installed into the core. */
typedef struct {
    void *ctx;
    esp_desktop_buddy_status_handler_t on_status;
    esp_desktop_buddy_name_handler_t on_name;
    esp_desktop_buddy_owner_handler_t on_owner;
    esp_desktop_buddy_unpair_handler_t on_unpair;
    esp_desktop_buddy_command_extension_set_t command_extension;
} esp_desktop_buddy_command_handlers_t;

/**
 * @brief Returns a successful command result.
 *
 * @return Successful command result wrapper.
 */
esp_desktop_buddy_command_result_t esp_desktop_buddy_command_ok(void);
/**
 * @brief Returns a failed command result with a standard error code and optional detail.
 *
 * Use `ESP_ERR_NOT_ALLOWED` when an application rejects a command for policy
 * or state reasons.
 *
 * @param err ESP-IDF error code for the failure.
 * @param detail Optional internal detail string for logs or diagnostics.
 *
 * @return Failed command result wrapper.
 */
esp_desktop_buddy_command_result_t esp_desktop_buddy_command_err(esp_err_t err, const char *detail);
/**
 * @brief Returns a successful status result backed by a serialized JSON object view.
 *
 * @param data Serialized JSON object bytes to expose as `status.data`.
 *
 * @return Successful status result wrapper.
 */
esp_desktop_buddy_status_reply_t esp_desktop_buddy_status_ok(esp_desktop_buddy_json_object_view_t data);
/**
 * @brief Returns a failed status result with a standard error code and optional detail.
 *
 * Use `ESP_ERR_NOT_ALLOWED` when status is unavailable because of application
 * policy or state.
 *
 * @param err ESP-IDF error code for the failure.
 * @param detail Optional internal detail string for logs or diagnostics.
 *
 * @return Failed status result wrapper.
 */
esp_desktop_buddy_status_reply_t esp_desktop_buddy_status_err(esp_err_t err, const char *detail);
#ifdef __cplusplus
}
#endif
