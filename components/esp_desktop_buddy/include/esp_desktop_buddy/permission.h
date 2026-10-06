/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Permission decisions that can be emitted for an active prompt. */
typedef enum {
    ESP_DESKTOP_BUDDY_PERMISSION_DECISION_ONCE = 0,
    ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY,
} esp_desktop_buddy_permission_decision_t;

/** Application-local actions that can be posted into the core. */
typedef enum {
    ESP_DESKTOP_BUDDY_PERMISSION_REPLY_ONCE = 0,
    ESP_DESKTOP_BUDDY_PERMISSION_REPLY_DENY,
} esp_desktop_buddy_permission_reply_type_t;

/** Local action payload for prompt replies keyed by prompt id. */
typedef struct {
    esp_desktop_buddy_permission_reply_type_t type;
    /*
     * Borrowed prompt id copied by esp_desktop_buddy_post_permission_reply() during the call.
     *
     * The caller retains ownership of the string storage.
     */
    const char *prompt_id;
} esp_desktop_buddy_permission_reply_t;

#ifdef __cplusplus
}
#endif
