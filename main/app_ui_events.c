/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "app_shared.h"

#include "esp_log.h"

static const char *TAG = "buddy_app_ui_events";

esp_err_t buddy_app_ui_events_init(buddy_app_t *app)
{
    if (app == NULL || app->mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app->prompt_reply_queue = xQueueCreate(BUDDY_APP_PROMPT_REPLY_QUEUE_LENGTH,
                                           sizeof(buddy_app_prompt_reply_command_t));
    if (app->prompt_reply_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void buddy_app_ui_notify(const buddy_app_t *app, uint32_t dirty_bits)
{
    TaskHandle_t task;

    if (app == NULL || dirty_bits == 0) {
        return;
    }
    task = app->ui_task;
    if (task != NULL) {
        (void)xTaskNotify(task, dirty_bits, eSetBits);
    }
}

uint32_t buddy_app_ui_wait_for_events(TickType_t timeout_ticks)
{
    uint32_t dirty_bits = 0;

    (void)xTaskNotifyWait(0, UINT32_MAX, &dirty_bits, timeout_ticks);
    return dirty_bits;
}

esp_err_t buddy_app_prompt_reply_enqueue(buddy_app_t *app,
                                         esp_desktop_buddy_permission_decision_t decision,
                                         const char *prompt_id)
{
    buddy_app_prompt_reply_command_t command = { .decision = decision };
    bool queue_failed = false;
    esp_err_t err = ESP_OK;

    if (app == NULL || app->mutex == NULL || prompt_id == NULL || prompt_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    if (!app->state_cache.prompt.present ||
        strcmp(app->state_cache.prompt.id, prompt_id) != 0) {
        app->prompt_reply_failed_count++;
        err = ESP_ERR_INVALID_STATE;
    } else if (strcmp(app->prompt_reply_queued_id, prompt_id) == 0 ||
               app->prompt_reply_inflight_id[0] != '\0' ||
               strcmp(app->prompt_reply_sent_id, prompt_id) == 0) {
        app->prompt_reply_failed_count++;
        err = ESP_ERR_INVALID_STATE;
    } else {
        strlcpy(command.prompt_id,
                prompt_id,
                sizeof(command.prompt_id));
        command.session_generation = app->buddy_transport_session_generation;
        if (app->prompt_reply_queue == NULL ||
            xQueueSend(app->prompt_reply_queue, &command, 0) != pdPASS) {
            strlcpy(app->prompt_reply_error_id,
                    command.prompt_id,
                    sizeof(app->prompt_reply_error_id));
            app->prompt_reply_dropped_count++;
            queue_failed = true;
            err = ESP_ERR_TIMEOUT;
        } else {
            strlcpy(app->prompt_reply_queued_id,
                    command.prompt_id,
                    sizeof(app->prompt_reply_queued_id));
            app->prompt_reply_error_id[0] = '\0';
        }
    }
    xSemaphoreGive(app->mutex);

    if (err != ESP_OK) {
        (void)buddy_app_activity_push_event(app,
                                            BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                            queue_failed ? BUDDY_APP_ACTIVITY_LEVEL_ERROR
                                                         : BUDDY_APP_ACTIVITY_LEVEL_WARNING,
                                            queue_failed ? BUDDY_APP_ACTIVITY_EVENT_PROMPT_QUEUE_FULL
                                                         : BUDDY_APP_ACTIVITY_EVENT_PROMPT_REPLY_IGNORED,
                                            NULL);
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
        return err;
    }

    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
    return ESP_OK;
}

void buddy_app_prompt_reply_process(buddy_app_t *app)
{
    buddy_app_prompt_reply_command_t command;

    if (app == NULL || app->prompt_reply_queue == NULL) {
        return;
    }

    while (xQueueReceive(app->prompt_reply_queue, &command, 0) == pdPASS) {
        bool valid;
        bool live;
        esp_desktop_buddy_t *buddy;
        esp_err_t err;

        xSemaphoreTake(app->mutex, portMAX_DELAY);
        valid = app->state_cache.prompt.present &&
                strcmp(app->state_cache.prompt.id, command.prompt_id) == 0 &&
                app->buddy_transport_session_generation == command.session_generation &&
                app->prompt_reply_inflight_id[0] == '\0' &&
                strcmp(app->prompt_reply_sent_id, command.prompt_id) != 0;
        if (strcmp(app->prompt_reply_queued_id, command.prompt_id) == 0) {
            app->prompt_reply_queued_id[0] = '\0';
        }
        buddy = app->buddy;
        live = valid && buddy != NULL && esp_desktop_buddy_is_live(buddy);
        err = ESP_ERR_INVALID_STATE;
        if (live) {
            strlcpy(app->prompt_reply_inflight_id,
                    command.prompt_id,
                    sizeof(app->prompt_reply_inflight_id));
            err = command.decision == ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY
                      ? esp_desktop_buddy_prompt_deny(buddy, command.prompt_id)
                      : esp_desktop_buddy_prompt_approve_once(buddy, command.prompt_id);
            if (err != ESP_OK) {
                app->prompt_reply_inflight_id[0] = '\0';
                strlcpy(app->prompt_reply_error_id,
                        command.prompt_id,
                        sizeof(app->prompt_reply_error_id));
                app->prompt_reply_failed_count++;
            }
        } else if (valid) {
            strlcpy(app->prompt_reply_error_id,
                    command.prompt_id,
                    sizeof(app->prompt_reply_error_id));
            app->prompt_reply_failed_count++;
        } else {
            app->prompt_reply_failed_count++;
        }
        xSemaphoreGive(app->mutex);

        if (!live) {
            (void)buddy_app_activity_push_event(app,
                                                BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                                BUDDY_APP_ACTIVITY_LEVEL_WARNING,
                                                BUDDY_APP_ACTIVITY_EVENT_PROMPT_EXPIRED,
                                                NULL);
            buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
            continue;
        }

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "prompt response failed: %s", esp_err_to_name(err));
            (void)buddy_app_activity_push_event(app,
                                                BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                                BUDDY_APP_ACTIVITY_LEVEL_ERROR,
                                                BUDDY_APP_ACTIVITY_EVENT_PROMPT_FAILED,
                                                NULL);
        } else {
            (void)buddy_app_activity_push_event(app,
                                                BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                                BUDDY_APP_ACTIVITY_LEVEL_INFO,
                                                command.decision == ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY
                                                    ? BUDDY_APP_ACTIVITY_EVENT_PROMPT_DENIAL_QUEUED
                                                    : BUDDY_APP_ACTIVITY_EVENT_PROMPT_APPROVAL_QUEUED,
                                                NULL);
        }
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
    }
}
