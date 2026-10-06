/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "example_app_helpers.h"
#include "example_status.h"

#include "app_shared.h"

#define BUDDY_APP_NVS_NAMESPACE "buddy"
#define BUDDY_APP_STATUS_BUF (ESP_DESKTOP_BUDDY_LINE_MAX + 1)
#define BUDDY_APP_CHARPACK_REQUEST_QUEUE_LENGTH 2
#define BUDDY_APP_CHARPACK_WORKER_STACK 4096
#define BUDDY_APP_CHARPACK_WORKER_PRIORITY 2

#if CONFIG_FREERTOS_UNICORE
#define BUDDY_APP_CHARPACK_WORKER_CORE 0
#else
#define BUDDY_APP_CHARPACK_WORKER_CORE 1
#endif

typedef enum {
    BUDDY_APP_CHARPACK_REQUEST_REFRESH = 0,
    BUDDY_APP_CHARPACK_REQUEST_SET_ACTIVE,
} buddy_app_charpack_request_kind_t;

typedef struct {
    buddy_app_charpack_request_kind_t kind;
    buddy_app_t *app;
    char pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1];
} buddy_app_charpack_request_t;

static const char *TAG = "esp32s3_buddy";
static QueueHandle_t s_charpack_request_queue;
static TaskHandle_t s_charpack_worker_task;
static portMUX_TYPE s_charpack_worker_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_charpack_worker_starting;

static void buddy_app_charpack_finish_refresh(buddy_app_t *app,
                                              const example_charpack_info_t *items,
                                              size_t count,
                                              esp_err_t result)
{
    size_t visible_count = count;

    if (visible_count > BUDDY_APP_CHARPACK_LIST_MAX) {
        visible_count = BUDDY_APP_CHARPACK_LIST_MAX;
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->charpack_list_pending = false;
    app->charpack_list_result = result;
    if (result == ESP_OK) {
        memset(app->installed_packs, 0, sizeof(app->installed_packs));
        if (visible_count > 0) {
            memcpy(app->installed_packs, items, visible_count * sizeof(*items));
        }
        app->installed_pack_count = visible_count;
        snprintf(app->pack_status,
                 sizeof(app->pack_status),
                 visible_count == 0 ? "No installed packs" : "Found %u packs",
                 (unsigned)visible_count);
    } else {
        snprintf(app->pack_status,
                 sizeof(app->pack_status),
                 "Pack list failed: %s",
                 esp_err_to_name(result));
    }
    xSemaphoreGive(app->mutex);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
}

static void buddy_app_charpack_finish_switch(buddy_app_t *app,
                                             const char *pack_id,
                                             esp_err_t result)
{
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->charpack_switch_pending = false;
    app->charpack_switch_result = result;
    if (result != ESP_OK) {
        snprintf(app->pack_status,
                 sizeof(app->pack_status),
                 "Pack switch failed: %s",
                 esp_err_to_name(result));
    }
    xSemaphoreGive(app->mutex);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);

    if (result == ESP_OK) {
        (void)pack_id;
        (void)buddy_app_charpack_refresh_async(app);
    }
}

static void buddy_app_charpack_worker(void *arg)
{
    buddy_app_charpack_request_t request;

    (void)arg;
    while (true) {
        example_charpack_t *charpack;
        esp_err_t result;

        if (xQueueReceive(s_charpack_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (request.app == NULL || request.app->mutex == NULL) {
            continue;
        }

        xSemaphoreTake(request.app->mutex, portMAX_DELAY);
        charpack = request.app->charpack;
        xSemaphoreGive(request.app->mutex);
        if (charpack == NULL) {
            if (request.kind == BUDDY_APP_CHARPACK_REQUEST_REFRESH) {
                buddy_app_charpack_finish_refresh(request.app, NULL, 0, ESP_ERR_INVALID_STATE);
            } else {
                buddy_app_charpack_finish_switch(request.app,
                                                 request.pack_id,
                                                 ESP_ERR_INVALID_STATE);
            }
            continue;
        }

        if (request.kind == BUDDY_APP_CHARPACK_REQUEST_REFRESH) {
            example_charpack_info_t items[BUDDY_APP_CHARPACK_LIST_MAX] = {0};
            size_t count = 0;

            result = example_charpack_list(charpack,
                                           items,
                                           BUDDY_APP_CHARPACK_LIST_MAX,
                                           &count);
            buddy_app_charpack_finish_refresh(request.app, items, count, result);
        } else {
            result = example_charpack_set_active(charpack, request.pack_id);
            buddy_app_charpack_finish_switch(request.app, request.pack_id, result);
        }
    }
}

static esp_err_t buddy_app_charpack_worker_start(void)
{
    QueueHandle_t queue;
    TaskHandle_t task = NULL;
    bool starting;

    portENTER_CRITICAL(&s_charpack_worker_lock);
    starting = s_charpack_worker_starting;
    if (s_charpack_worker_task == NULL && !starting) {
        s_charpack_worker_starting = true;
    }
    portEXIT_CRITICAL(&s_charpack_worker_lock);

    if (s_charpack_worker_task != NULL) {
        return ESP_OK;
    }
    if (starting) {
        return ESP_ERR_INVALID_STATE;
    }

    queue = xQueueCreate(BUDDY_APP_CHARPACK_REQUEST_QUEUE_LENGTH,
                         sizeof(buddy_app_charpack_request_t));
    if (queue == NULL) {
        portENTER_CRITICAL(&s_charpack_worker_lock);
        s_charpack_worker_starting = false;
        portEXIT_CRITICAL(&s_charpack_worker_lock);
        return ESP_ERR_NO_MEM;
    }
    s_charpack_request_queue = queue;
    if (xTaskCreatePinnedToCore(buddy_app_charpack_worker,
                                "buddy_charpack",
                                BUDDY_APP_CHARPACK_WORKER_STACK,
                                NULL,
                                BUDDY_APP_CHARPACK_WORKER_PRIORITY,
                                &task,
                                BUDDY_APP_CHARPACK_WORKER_CORE) != pdPASS) {
        vQueueDelete(queue);
        s_charpack_request_queue = NULL;
        portENTER_CRITICAL(&s_charpack_worker_lock);
        s_charpack_worker_starting = false;
        portEXIT_CRITICAL(&s_charpack_worker_lock);
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL(&s_charpack_worker_lock);
    s_charpack_worker_task = task;
    s_charpack_worker_starting = false;
    portEXIT_CRITICAL(&s_charpack_worker_lock);
    return ESP_OK;
}

static esp_err_t buddy_app_charpack_queue_request(buddy_app_charpack_request_t *request)
{
    esp_err_t result = buddy_app_charpack_worker_start();

    if (result != ESP_OK) {
        return result;
    }
    return xQueueSend(s_charpack_request_queue, request, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t buddy_app_charpack_refresh_async(buddy_app_t *app)
{
    buddy_app_charpack_request_t request = {
        .kind = BUDDY_APP_CHARPACK_REQUEST_REFRESH,
        .app = app,
    };
    bool duplicate;
    bool available;
    esp_err_t result;

    if (app == NULL || app->mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    duplicate = app->charpack_list_pending;
    available = app->charpack != NULL;
    if (!duplicate && available) {
        app->charpack_list_pending = true;
        app->charpack_list_result = ESP_ERR_INVALID_STATE;
        strlcpy(app->pack_status, "Refreshing character packs", sizeof(app->pack_status));
    } else if (!duplicate) {
        app->charpack_list_pending = false;
        app->charpack_list_result = ESP_ERR_INVALID_STATE;
        strlcpy(app->pack_status, "Character packs unavailable", sizeof(app->pack_status));
    }
    xSemaphoreGive(app->mutex);
    if (!duplicate) {
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
    }

    if (duplicate || !available) {
        return ESP_ERR_INVALID_STATE;
    }

    result = buddy_app_charpack_queue_request(&request);
    if (result != ESP_OK) {
        buddy_app_charpack_finish_refresh(app, NULL, 0, result);
    }
    return result;
}

esp_err_t buddy_app_charpack_set_active_async(buddy_app_t *app, const char *pack_id)
{
    buddy_app_charpack_request_t request = {
        .kind = BUDDY_APP_CHARPACK_REQUEST_SET_ACTIVE,
        .app = app,
    };
    size_t pack_id_length;
    bool duplicate;
    bool available;
    esp_err_t result;

    if (app == NULL || app->mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pack_id_length = pack_id == NULL ? 0 : strnlen(pack_id, sizeof(request.pack_id));
    if (pack_id == NULL || pack_id_length == 0 || pack_id_length >= sizeof(request.pack_id)) {
        buddy_app_charpack_finish_switch(app, "", ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(request.pack_id, pack_id, pack_id_length);

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    duplicate = app->charpack_switch_pending;
    available = app->charpack != NULL;
    if (!duplicate && available) {
        app->charpack_switch_pending = true;
        app->charpack_switch_result = ESP_ERR_INVALID_STATE;
        snprintf(app->pack_status,
                 sizeof(app->pack_status),
                 "Switching to %s",
                 request.pack_id);
    } else if (!duplicate) {
        app->charpack_switch_pending = false;
        app->charpack_switch_result = ESP_ERR_INVALID_STATE;
        strlcpy(app->pack_status, "Character packs unavailable", sizeof(app->pack_status));
    }
    xSemaphoreGive(app->mutex);
    if (!duplicate) {
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
    }

    if (duplicate || !available) {
        return ESP_ERR_INVALID_STATE;
    }

    result = buddy_app_charpack_queue_request(&request);
    if (result != ESP_OK) {
        buddy_app_charpack_finish_switch(app, request.pack_id, result);
    }
    return result;
}

static const char *buddy_app_pack_mode_name(example_charpack_mode_t mode)
{
    switch (mode) {
    case EXAMPLE_CHARPACK_MODE_GIF:
        return "gif";
    case EXAMPLE_CHARPACK_MODE_TEXT:
        return "text";
    default:
        return "unknown";
    }
}

void buddy_app_init(buddy_app_t *app)
{
    if (app == NULL) {
        return;
    }

    memset(app, 0, sizeof(*app));
    example_safe_copy(app->display_name, sizeof(app->display_name), "ESP32-S3 Buddy");
    app->advertising_name[0] = '\0';
    example_safe_copy(app->pack_status, sizeof(app->pack_status), "No active pack");
    app->charpack_list_result = ESP_ERR_INVALID_STATE;
    app->charpack_switch_result = ESP_OK;
    app->aod_timeout_minutes = 30;
    app->display_brightness_percent = 80;
    app->aod_brightness_percent = 8;
    app->tz_offset_seconds = 0;
}

esp_desktop_buddy_status_reply_t buddy_app_status_handler(void *ctx, esp_desktop_buddy_t *buddy)
{
    buddy_app_t *app = (buddy_app_t *)ctx;
    static char status_json[BUDDY_APP_STATUS_BUF];
    esp_desktop_buddy_transport_ble_state_t transport_state = {0};
    char display_name[BUDDY_APP_NAME_MAX];
    char owner_name[BUDDY_APP_OWNER_MAX];
    char pack_status[BUDDY_APP_STATUS_MAX];
    uint32_t approvals;
    uint32_t denials;
    uint32_t nap_seconds;
    uint32_t level;
    uint32_t velocity;
    uint64_t fs_total = 0;
    uint64_t fs_free = 0;
    uint64_t tokens;
    bool have_active_pack;
    example_charpack_info_t active_pack = {0};
    example_progress_state_t progress = {0};
    example_status_doc_t doc = {0};
    cJSON *pack = NULL;
    (void)buddy;

    if (app->transport != NULL) {
        esp_desktop_buddy_transport_ble_get_state(app->transport, &transport_state);
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    example_safe_copy(display_name, sizeof(display_name), app->display_name);
    example_safe_copy(owner_name, sizeof(owner_name), app->owner_name);
    example_safe_copy(pack_status, sizeof(pack_status), app->pack_status);
    approvals = app->approval_count;
    denials = app->denial_count;
    nap_seconds = app->nap_seconds;
    tokens = app->state_cache.tokens;
    have_active_pack = app->have_active_pack;
    active_pack = app->active_pack;
    progress = app->progress;
    xSemaphoreGive(app->mutex);

    level = example_progress_level(tokens);
    velocity = example_progress_velocity(&progress);

    (void)esp_vfs_fat_info(CONFIG_EXAMPLE_CHARPACK_MOUNT_POINT, &fs_total, &fs_free);

    if (example_status_begin(&doc,
                               display_name,
                               owner_name,
                               transport_state.encrypted,
                               approvals,
                               denials) != ESP_OK) {
        return esp_desktop_buddy_status_err(ESP_FAIL, "status_begin_failed");
    }
    pack = cJSON_AddObjectToObject(doc.root, "pack");
    if (pack == NULL) {
        cJSON_Delete(doc.root);
        return esp_desktop_buddy_status_err(ESP_ERR_NO_MEM, "status_section_alloc_failed");
    }
    cJSON_AddNumberToObject(doc.sys, "fsFree", (double)fs_free);
    cJSON_AddNumberToObject(doc.sys, "fsTotal", (double)fs_total);
    cJSON_AddNumberToObject(doc.stats, "vel", (double)velocity);
    cJSON_AddNumberToObject(doc.stats, "nap", (double)nap_seconds);
    cJSON_AddNumberToObject(doc.stats, "lvl", (double)level);
    cJSON_AddBoolToObject(pack, "hasActive", have_active_pack);
    cJSON_AddStringToObject(pack, "active", have_active_pack ? active_pack.pack_id : "");
    cJSON_AddStringToObject(pack,
                            "mode",
                            have_active_pack ? buddy_app_pack_mode_name(active_pack.mode) : "");
    cJSON_AddStringToObject(pack, "status", pack_status);
    return example_status_from_json(doc.root, status_json, sizeof(status_json));
}

esp_desktop_buddy_command_result_t buddy_app_name_handler(void *ctx, esp_desktop_buddy_t *buddy, const char *name)
{
    buddy_app_t *app = (buddy_app_t *)ctx;

    (void)buddy;
    return example_update_persisted_string(app->mutex,
                                             app->display_name,
                                             sizeof(app->display_name),
                                             name,
                                             BUDDY_APP_NVS_NAMESPACE,
                                             "display_name");
}

esp_desktop_buddy_command_result_t buddy_app_owner_handler(void *ctx, esp_desktop_buddy_t *buddy, const char *name)
{
    buddy_app_t *app = (buddy_app_t *)ctx;

    (void)buddy;
    return example_update_string_field(app->mutex,
                                       app->owner_name,
                                       sizeof(app->owner_name),
                                       name);
}

esp_desktop_buddy_command_result_t buddy_app_unpair_handler(void *ctx, esp_desktop_buddy_t *buddy)
{
    buddy_app_t *app = (buddy_app_t *)ctx;

    (void)buddy;
    return example_clear_bonds(app->transport);
}

void buddy_app_buddy_event(void *ctx, const esp_desktop_buddy_event_t *event)
{
    buddy_app_t *app = (buddy_app_t *)ctx;

    if (app == NULL || event == NULL) {
        return;
    }

    switch (event->type) {
    case ESP_DESKTOP_BUDDY_EVENT_SNAPSHOT_UPDATED: {
        example_buddy_state_cache_t state_cache = {0};
        bool prompt_changed;
        bool previous_prompt_present;

        if (example_buddy_state_cache_refresh(app->buddy, &state_cache) != ESP_OK) {
            break;
        }
        xSemaphoreTake(app->mutex, portMAX_DELAY);
        previous_prompt_present = app->state_cache.prompt.present;
        prompt_changed = previous_prompt_present != state_cache.prompt.present ||
                         strcmp(app->state_cache.prompt.id, state_cache.prompt.id) != 0;
        example_progress_note_snapshot(&app->progress, &app->state_cache, &state_cache);
        app->state_cache = state_cache;
        if (prompt_changed) {
            app->prompt_reply_queued_id[0] = '\0';
            app->prompt_reply_sent_id[0] = '\0';
            app->prompt_reply_error_id[0] = '\0';
        }
        xSemaphoreGive(app->mutex);
        if (prompt_changed && state_cache.prompt.present) {
            (void)buddy_app_activity_push_event(app,
                                                BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                                BUDDY_APP_ACTIVITY_LEVEL_INFO,
                                                BUDDY_APP_ACTIVITY_EVENT_PERMISSION_RECEIVED,
                                                NULL);
        } else if (prompt_changed && previous_prompt_present) {
            (void)buddy_app_activity_push_event(app,
                                                BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                                BUDDY_APP_ACTIVITY_LEVEL_INFO,
                                                BUDDY_APP_ACTIVITY_EVENT_PERMISSION_CLOSED,
                                                NULL);
        }
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
        break;
    }
    case ESP_DESKTOP_BUDDY_EVENT_LIVENESS_CHANGED:
        if (!event->data.live) {
            xSemaphoreTake(app->mutex, portMAX_DELAY);
            example_buddy_state_cache_reset(&app->state_cache);
            example_progress_clear_prompt_timing(&app->progress);
            app->prompt_reply_queued_id[0] = '\0';
            app->prompt_reply_inflight_id[0] = '\0';
            app->prompt_reply_sent_id[0] = '\0';
            app->prompt_reply_error_id[0] = '\0';
            xSemaphoreGive(app->mutex);
            (void)buddy_app_activity_push_event(app,
                                                BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                                BUDDY_APP_ACTIVITY_LEVEL_WARNING,
                                                BUDDY_APP_ACTIVITY_EVENT_BUDDY_DISCONNECTED,
                                                NULL);
        }
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
        break;
    case ESP_DESKTOP_BUDDY_EVENT_PERMISSION_SENT:
        xSemaphoreTake(app->mutex, portMAX_DELAY);
        example_progress_note_permission_sent(&app->progress, event->data.permission_sent.decision);
        if (strcmp(app->prompt_reply_inflight_id, event->data.permission_sent.prompt_id) == 0) {
            app->prompt_reply_inflight_id[0] = '\0';
        }
        strlcpy(app->prompt_reply_sent_id,
                event->data.permission_sent.prompt_id,
                sizeof(app->prompt_reply_sent_id));
        app->prompt_reply_error_id[0] = '\0';
        xSemaphoreGive(app->mutex);
        example_note_prompt_response(app->mutex,
                                       &app->approval_count,
                                       &app->denial_count,
                                       event->data.permission_sent.decision);
        (void)buddy_app_activity_push_event(app,
                                            BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
                                            BUDDY_APP_ACTIVITY_LEVEL_INFO,
                                            event->data.permission_sent.decision ==
                                                    ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY
                                                ? BUDDY_APP_ACTIVITY_EVENT_PERMISSION_DENIED
                                                : BUDDY_APP_ACTIVITY_EVENT_PERMISSION_APPROVED,
                                            NULL);
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
        break;
    case ESP_DESKTOP_BUDDY_EVENT_TIME_SYNC:
        {
            esp_err_t time_err = buddy_app_time_apply_ble_sync(app, &event->data.time_sync);
            if (time_err != ESP_OK) ESP_LOGW(TAG, "BLE time rejected: %s", esp_err_to_name(time_err));
        }
        break;
    case ESP_DESKTOP_BUDDY_EVENT_ERROR:
        ESP_LOGW(TAG,
                 "buddy error kind=%d detail=%lu",
                 event->data.error.kind,
                 (unsigned long)event->data.error.detail);
        if (event->data.error.kind == ESP_DESKTOP_BUDDY_ERROR_ACTION) {
            char failed_prompt_id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];

            xSemaphoreTake(app->mutex, portMAX_DELAY);
            strlcpy(failed_prompt_id,
                    app->prompt_reply_inflight_id,
                    sizeof(failed_prompt_id));
            app->prompt_reply_inflight_id[0] = '\0';
            if (failed_prompt_id[0] != '\0' &&
                app->state_cache.prompt.present &&
                strcmp(failed_prompt_id, app->state_cache.prompt.id) == 0) {
                strlcpy(app->prompt_reply_error_id,
                        failed_prompt_id,
                        sizeof(app->prompt_reply_error_id));
            }
            app->prompt_reply_failed_count++;
            xSemaphoreGive(app->mutex);
        }
        (void)buddy_app_activity_push_event(
            app,
            BUDDY_APP_ACTIVITY_SOURCE_BUDDY,
            BUDDY_APP_ACTIVITY_LEVEL_ERROR,
            event->data.error.kind == ESP_DESKTOP_BUDDY_ERROR_ACTION
                ? BUDDY_APP_ACTIVITY_EVENT_PROMPT_BACKEND_FAILED
                : BUDDY_APP_ACTIVITY_EVENT_BUDDY_PROTOCOL_ERROR,
            NULL);
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
        break;
    default:
        break;
    }
}

void buddy_app_transport_event(void *ctx, const esp_desktop_buddy_transport_ble_event_t *event)
{
    buddy_app_t *app = (buddy_app_t *)ctx;
    esp_desktop_buddy_transport_ble_state_t previous = {0};
    esp_desktop_buddy_transport_ble_state_t current = {0};
    bool reset_session = false;

    if (app == NULL || event == NULL) {
        return;
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    previous = app->transport_state;
    xSemaphoreGive(app->mutex);
    example_update_transport_state(app->mutex,
                                   &app->transport_state,
                                   NULL,
                                   0,
                                   event);
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    current = app->transport_state;
    if ((previous.connected && !current.connected) ||
        (previous.tx_ready && !current.tx_ready)) {
        example_buddy_state_cache_reset(&app->state_cache);
        example_progress_clear_prompt_timing(&app->progress);
        app->buddy_transport_session_generation++;
        if (app->prompt_reply_queue != NULL) {
            xQueueReset(app->prompt_reply_queue);
        }
        app->prompt_reply_queued_id[0] = '\0';
        app->prompt_reply_inflight_id[0] = '\0';
        app->prompt_reply_sent_id[0] = '\0';
        app->prompt_reply_error_id[0] = '\0';
        reset_session = true;
    }
    xSemaphoreGive(app->mutex);
    if (reset_session && app->folder_push != NULL) {
        esp_desktop_buddy_folder_push_abort(app->folder_push);
    }
    if (previous.connected != current.connected) {
        (void)buddy_app_activity_push_event(app,
                                            BUDDY_APP_ACTIVITY_SOURCE_BLE,
                                            current.connected ? BUDDY_APP_ACTIVITY_LEVEL_INFO
                                                              : BUDDY_APP_ACTIVITY_LEVEL_WARNING,
                                            current.connected ? BUDDY_APP_ACTIVITY_EVENT_BLE_CONNECTED
                                                              : BUDDY_APP_ACTIVITY_EVENT_BLE_DISCONNECTED,
                                            NULL);
    } else if (previous.encrypted != current.encrypted && current.encrypted) {
        (void)buddy_app_activity_push_event(app,
                                            BUDDY_APP_ACTIVITY_SOURCE_BLE,
                                            BUDDY_APP_ACTIVITY_LEVEL_INFO,
                                            BUDDY_APP_ACTIVITY_EVENT_BLE_ENCRYPTED,
                                            NULL);
    }
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
}

void buddy_app_charpack_event(void *ctx, const example_charpack_event_t *event)
{
    buddy_app_t *app = (buddy_app_t *)ctx;
    static int64_t last_high_throughput_request_us;
    example_charpack_info_t active_info = {0};
    bool refresh_active = false;
    buddy_app_activity_event_t activity_event = BUDDY_APP_ACTIVITY_EVENT_GENERIC;
    buddy_app_activity_level_t activity_level = BUDDY_APP_ACTIVITY_LEVEL_INFO;

    if (app == NULL || event == NULL) {
        return;
    }

    if (event->type == EXAMPLE_CHARPACK_EVENT_TRANSFER_STARTED) {
        last_high_throughput_request_us = esp_timer_get_time();
        (void)esp_desktop_buddy_transport_ble_set_high_throughput(app->transport, true);
    } else if (event->type == EXAMPLE_CHARPACK_EVENT_TRANSFER_ABORTED ||
               event->type == EXAMPLE_CHARPACK_EVENT_INSTALL_SUCCEEDED ||
               event->type == EXAMPLE_CHARPACK_EVENT_INSTALL_FAILED) {
        (void)esp_desktop_buddy_transport_ble_set_high_throughput(app->transport, false);
    }

    if (event->type == EXAMPLE_CHARPACK_EVENT_TRANSFER_PROGRESS) {
        static int64_t last_progress_notify_us;
        const int64_t now_us = esp_timer_get_time();

        if (now_us - last_high_throughput_request_us >= 600000) {
            last_high_throughput_request_us = now_us;
            (void)esp_desktop_buddy_transport_ble_set_high_throughput(app->transport, true);
        }

        if (now_us - last_progress_notify_us >= 200000LL) {
            last_progress_notify_us = now_us;
            buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
        }
        return;
    }

    if (event->type == EXAMPLE_CHARPACK_EVENT_INSTALL_SUCCEEDED &&
        app->charpack != NULL &&
        example_charpack_get_active(app->charpack, &active_info) == ESP_OK &&
        strcmp(active_info.pack_id, event->info.pack_id) == 0) {
        refresh_active = true;
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    switch (event->type) {
    case EXAMPLE_CHARPACK_EVENT_INSTALL_SUCCEEDED:
        if (refresh_active) {
            app->active_pack = active_info;
            app->have_active_pack = true;
        }
        snprintf(app->pack_status, sizeof(app->pack_status), "Installed %s", event->info.pack_id);
        activity_event = BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALLED;
        break;
    case EXAMPLE_CHARPACK_EVENT_INSTALL_FAILED:
        snprintf(app->pack_status, sizeof(app->pack_status), "Install failed %s", event->info.pack_id);
        activity_event = BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALL_FAILED;
        activity_level = BUDDY_APP_ACTIVITY_LEVEL_ERROR;
        break;
    case EXAMPLE_CHARPACK_EVENT_TRANSFER_STARTED:
        snprintf(app->pack_status, sizeof(app->pack_status), "Receiving %s", event->info.pack_id);
        activity_event = BUDDY_APP_ACTIVITY_EVENT_PACK_TRANSFER_STARTED;
        break;
    case EXAMPLE_CHARPACK_EVENT_ACTIVE_CHANGED:
        app->active_pack = event->info;
        app->have_active_pack = true;
        snprintf(app->pack_status, sizeof(app->pack_status), "Active %s", event->info.pack_id);
        activity_event = BUDDY_APP_ACTIVITY_EVENT_PACK_CHANGED;
        break;
    case EXAMPLE_CHARPACK_EVENT_ACTIVE_CLEARED:
        app->have_active_pack = false;
        snprintf(app->pack_status, sizeof(app->pack_status), "No active pack");
        activity_event = BUDDY_APP_ACTIVITY_EVENT_PACK_CLEARED;
        break;
    default:
        break;
    }
    xSemaphoreGive(app->mutex);
    if (activity_event != BUDDY_APP_ACTIVITY_EVENT_GENERIC) {
        (void)buddy_app_activity_push_event(app,
                                            BUDDY_APP_ACTIVITY_SOURCE_PACK,
                                            activity_level,
                                            activity_event,
                                            NULL);
    }
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY | BUDDY_APP_UI_DIRTY_ACTIVITY);
    if (event->type == EXAMPLE_CHARPACK_EVENT_INSTALL_SUCCEEDED) {
        (void)buddy_app_charpack_refresh_async(app);
    }
}


void buddy_app_print_state(buddy_app_t *app, FILE *out)
{
    if (app == NULL || out == NULL) return;
    buddy_app_diag_print(app, out);
}
