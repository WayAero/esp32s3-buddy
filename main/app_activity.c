/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <string.h>
#include <time.h>

#include "app_shared.h"

#define BUDDY_APP_ACTIVITY_SNAPSHOT_TIMEOUT_MS 20

#include "esp_timer.h"

static const char *buddy_app_activity_event_token(buddy_app_activity_event_t event)
{
    static const char *const tokens[] = {
        [BUDDY_APP_ACTIVITY_EVENT_GENERIC] = "generic",
        [BUDDY_APP_ACTIVITY_EVENT_SENSITIVE_OMITTED] = "sensitive_omitted",
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_RECEIVED] = "permission_received",
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_CLOSED] = "permission_closed",
        [BUDDY_APP_ACTIVITY_EVENT_BUDDY_DISCONNECTED] = "buddy_disconnected",
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_APPROVED] = "permission_approved",
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_DENIED] = "permission_denied",
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_BACKEND_FAILED] = "prompt_backend_failed",
        [BUDDY_APP_ACTIVITY_EVENT_BUDDY_PROTOCOL_ERROR] = "buddy_protocol_error",
        [BUDDY_APP_ACTIVITY_EVENT_BLE_CONNECTED] = "ble_connected",
        [BUDDY_APP_ACTIVITY_EVENT_BLE_DISCONNECTED] = "ble_disconnected",
        [BUDDY_APP_ACTIVITY_EVENT_BLE_ENCRYPTED] = "ble_encrypted",
        [BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALLED] = "pack_installed",
        [BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALL_FAILED] = "pack_install_failed",
        [BUDDY_APP_ACTIVITY_EVENT_PACK_TRANSFER_STARTED] = "pack_transfer_started",
        [BUDDY_APP_ACTIVITY_EVENT_PACK_CHANGED] = "pack_changed",
        [BUDDY_APP_ACTIVITY_EVENT_PACK_CLEARED] = "pack_cleared",
        [BUDDY_APP_ACTIVITY_EVENT_SETTINGS_RESET] = "settings_reset",
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_REPLY_IGNORED] = "prompt_reply_ignored",
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_QUEUE_FULL] = "prompt_queue_full",
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_EXPIRED] = "prompt_expired",
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_FAILED] = "prompt_failed",
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_APPROVAL_QUEUED] = "prompt_approval_queued",
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_DENIAL_QUEUED] = "prompt_denial_queued",
    };

    return event < sizeof(tokens) / sizeof(tokens[0]) && tokens[event] != NULL ? tokens[event] : "generic";
}

static bool buddy_app_activity_is_safe_token(const char *value)
{
    size_t length;

    if (value == NULL || value[0] == '\0') {
        return false;
    }
    length = strnlen(value, BUDDY_APP_ACTIVITY_PARAM_MAX);
    if (length == 0 || length >= BUDDY_APP_ACTIVITY_PARAM_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)value[i];

        if (!isalnum(ch) && ch != '_' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return true;
}

static esp_err_t buddy_app_activity_push_internal(buddy_app_t *app,
                                                  buddy_app_activity_source_t source,
                                                  buddy_app_activity_level_t level,
                                                  buddy_app_activity_event_t event,
                                                  const char *parameter)
{
    buddy_app_activity_entry_t *entry;
    uint32_t uptime_seconds;

    if (app == NULL || app->mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000LL);

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    entry = &app->activity[app->activity_next_index];
    *entry = (buddy_app_activity_entry_t){
        .sequence = ++app->activity_next_sequence,
        .uptime_seconds = uptime_seconds,
        .wall_time_seconds = app->time_source == BUDDY_APP_TIME_UNSYNCED ? 0 : time(NULL),
        .tz_offset_seconds = app->tz_offset_seconds,
        .source = source,
        .level = level,
        .event = event,
    };
    if (parameter != NULL) {
        strlcpy(entry->parameter, parameter, sizeof(entry->parameter));
    }
    strlcpy(entry->message, buddy_app_activity_event_token(event), sizeof(entry->message));
    app->activity_next_index = (uint8_t)((app->activity_next_index + 1U) % BUDDY_APP_ACTIVITY_CAPACITY);
    if (app->activity_count < BUDDY_APP_ACTIVITY_CAPACITY) {
        app->activity_count++;
    }
    xSemaphoreGive(app->mutex);
    return ESP_OK;
}

esp_err_t buddy_app_activity_push_event(buddy_app_t *app,
                                        buddy_app_activity_source_t source,
                                        buddy_app_activity_level_t level,
                                        buddy_app_activity_event_t event,
                                        const char *parameter)
{
    if (event == BUDDY_APP_ACTIVITY_EVENT_GENERIC ||
        event >= BUDDY_APP_ACTIVITY_EVENT_PROMPT_DENIAL_QUEUED + 1U ||
        (parameter != NULL && !buddy_app_activity_is_safe_token(parameter))) {
        return ESP_ERR_INVALID_ARG;
    }
    return buddy_app_activity_push_internal(app, source, level, event, parameter);
}

esp_err_t buddy_app_activity_push_generic(buddy_app_t *app,
                                          buddy_app_activity_source_t source,
                                          buddy_app_activity_level_t level,
                                          const char *safe_code)
{
    if (!buddy_app_activity_is_safe_token(safe_code)) {
        return ESP_ERR_INVALID_ARG;
    }
    return buddy_app_activity_push_internal(app,
                                            source,
                                            level,
                                            BUDDY_APP_ACTIVITY_EVENT_GENERIC,
                                            safe_code);
}

size_t buddy_app_activity_snapshot_newest(buddy_app_t *app,
                                           buddy_app_activity_entry_t *entries,
                                           size_t capacity)
{
    size_t count;

    if (app == NULL || app->mutex == NULL || entries == NULL || capacity == 0) {
        return 0;
    }

    if (xSemaphoreTake(app->mutex,
                       pdMS_TO_TICKS(BUDDY_APP_ACTIVITY_SNAPSHOT_TIMEOUT_MS)) != pdTRUE) {
        return 0;
    }
    count = app->activity_count < capacity ? app->activity_count : capacity;
    for (size_t i = 0; i < count; ++i) {
        uint8_t index = (uint8_t)((app->activity_next_index + BUDDY_APP_ACTIVITY_CAPACITY - 1U - i) %
                                  BUDDY_APP_ACTIVITY_CAPACITY);

        entries[i] = app->activity[index];
    }
    xSemaphoreGive(app->mutex);
    return count;
}
