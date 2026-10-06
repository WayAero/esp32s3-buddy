/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/command_extensions.h"
#include "esp_desktop_buddy/folder_push.h"
#include "esp_desktop_buddy/transport_ble.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "example_app_helpers.h"
#include "example_charpack.h"

#define BUDDY_APP_NAME_MAX 32
#define BUDDY_APP_BLE_NAME_MAX (ESP_DESKTOP_BUDDY_TRANSPORT_BLE_NAME_MAX + 1)
#define BUDDY_APP_OWNER_MAX 32
#define BUDDY_APP_STATUS_MAX 64
#define BUDDY_APP_CHARPACK_LIST_MAX 8

enum {
    BUDDY_APP_CAP_BLE = 1U << 1,
    BUDDY_APP_CAP_STORAGE = 1U << 4,
    BUDDY_APP_CAP_TIME = 1U << 5,
};
#define BUDDY_APP_ACTIVITY_CAPACITY 32
#define BUDDY_APP_ACTIVITY_MESSAGE_MAX 72
#define BUDDY_APP_ACTIVITY_PARAM_MAX 24
#define BUDDY_APP_PROMPT_REPLY_QUEUE_LENGTH 8
#define BUDDY_APP_DIAG_SERVICE_ERROR_MAX 64

typedef enum {
    BUDDY_APP_TIME_UNSYNCED = 0,
    BUDDY_APP_TIME_BLE_SYNCED,
} buddy_app_time_source_t;

typedef enum {
    BUDDY_APP_ACTIVITY_SOURCE_BUDDY = 0,
    BUDDY_APP_ACTIVITY_SOURCE_BLE,
    BUDDY_APP_ACTIVITY_SOURCE_PACK,
    BUDDY_APP_ACTIVITY_SOURCE_SYSTEM,
} buddy_app_activity_source_t;

typedef enum {
    BUDDY_APP_ACTIVITY_LEVEL_INFO = 0,
    BUDDY_APP_ACTIVITY_LEVEL_WARNING,
    BUDDY_APP_ACTIVITY_LEVEL_ERROR,
} buddy_app_activity_level_t;

typedef enum {
    BUDDY_APP_ACTIVITY_EVENT_GENERIC = 0,
    BUDDY_APP_ACTIVITY_EVENT_SENSITIVE_OMITTED,
    BUDDY_APP_ACTIVITY_EVENT_PERMISSION_RECEIVED,
    BUDDY_APP_ACTIVITY_EVENT_PERMISSION_CLOSED,
    BUDDY_APP_ACTIVITY_EVENT_BUDDY_DISCONNECTED,
    BUDDY_APP_ACTIVITY_EVENT_PERMISSION_APPROVED,
    BUDDY_APP_ACTIVITY_EVENT_PERMISSION_DENIED,
    BUDDY_APP_ACTIVITY_EVENT_PROMPT_BACKEND_FAILED,
    BUDDY_APP_ACTIVITY_EVENT_BUDDY_PROTOCOL_ERROR,
    BUDDY_APP_ACTIVITY_EVENT_BLE_CONNECTED,
    BUDDY_APP_ACTIVITY_EVENT_BLE_DISCONNECTED,
    BUDDY_APP_ACTIVITY_EVENT_BLE_ENCRYPTED,
    BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALLED,
    BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALL_FAILED,
    BUDDY_APP_ACTIVITY_EVENT_PACK_TRANSFER_STARTED,
    BUDDY_APP_ACTIVITY_EVENT_PACK_CHANGED,
    BUDDY_APP_ACTIVITY_EVENT_PACK_CLEARED,
    BUDDY_APP_ACTIVITY_EVENT_SETTINGS_RESET,
    BUDDY_APP_ACTIVITY_EVENT_PROMPT_REPLY_IGNORED,
    BUDDY_APP_ACTIVITY_EVENT_PROMPT_QUEUE_FULL,
    BUDDY_APP_ACTIVITY_EVENT_PROMPT_EXPIRED,
    BUDDY_APP_ACTIVITY_EVENT_PROMPT_FAILED,
    BUDDY_APP_ACTIVITY_EVENT_PROMPT_APPROVAL_QUEUED,
    BUDDY_APP_ACTIVITY_EVENT_PROMPT_DENIAL_QUEUED,
} buddy_app_activity_event_t;

typedef struct {
    uint32_t sequence;
    uint32_t uptime_seconds;
    int64_t wall_time_seconds;
    int32_t tz_offset_seconds; /* 事件发生时的偏移，后续校时不重写历史。 */
    buddy_app_activity_source_t source;
    buddy_app_activity_level_t level;
    buddy_app_activity_event_t event;
    char parameter[BUDDY_APP_ACTIVITY_PARAM_MAX];
    /* Legacy UI compatibility. Contains an event token, never an English sentence. */
    char message[BUDDY_APP_ACTIVITY_MESSAGE_MAX];
} buddy_app_activity_entry_t;

typedef enum {
    BUDDY_APP_DIAG_TASK_UI = 0,
    BUDDY_APP_DIAG_TASK_SETTINGS,
    BUDDY_APP_DIAG_TASK_STATUS_LED,
    BUDDY_APP_DIAG_TASK_COUNT,
} buddy_app_diag_task_index_t;

typedef struct {
    char version[32];
    char git_commit[32];
    char idf_version[32];
    char build[32];
    uint32_t internal_free;
    uint32_t internal_min_free;
    uint32_t internal_largest;
    uint32_t dma_free;
    uint32_t dma_min_free;
    uint32_t dma_largest;
    uint32_t psram_free;
    uint32_t psram_min_free;
    uint32_t psram_largest;
    uint32_t task_stack_bytes[BUDDY_APP_DIAG_TASK_COUNT];
    uint8_t activity_count;
    uint32_t prompt_reply_dropped_count;
    uint32_t prompt_reply_failed_count;
    char service_error[BUDDY_APP_DIAG_SERVICE_ERROR_MAX];
} buddy_app_diag_snapshot_t;

typedef struct {
    esp_desktop_buddy_permission_decision_t decision;
    uint32_t session_generation;
    char prompt_id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];
} buddy_app_prompt_reply_command_t;

enum {
    BUDDY_APP_UI_DIRTY_BUDDY = 1U << 0,
    BUDDY_APP_UI_DIRTY_TIME = 1U << 5,
    BUDDY_APP_UI_DIRTY_SETTINGS = 1U << 6,
    BUDDY_APP_UI_DIRTY_LAYOUT = 1U << 7,
    BUDDY_APP_UI_DIRTY_ACTIVITY = 1U << 8,
};

typedef enum {
    BUDDY_APP_THEME_DEEPSEEK = 0,
    BUDDY_APP_THEME_CLAUDE,
    /* 保留已保存的浅色主题值 0/1；深色主题追加为 2/3。 */
    BUDDY_APP_THEME_DEEPSEEK_DARK,
    BUDDY_APP_THEME_CLAUDE_DARK,
    BUDDY_APP_THEME_COUNT,
} buddy_app_theme_t;

typedef enum {
    BUDDY_APP_ROTATION_PORTRAIT = 1, /* 保持旧 NVS 的手动方向值；旧值 0 迁移为竖屏。 */
    BUDDY_APP_ROTATION_LANDSCAPE,
    BUDDY_APP_ROTATION_MODE_COUNT,
} buddy_app_rotation_mode_t;

typedef struct {
    SemaphoreHandle_t mutex;
    uint32_t available_caps;
    esp_desktop_buddy_t *buddy;
    esp_desktop_buddy_transport_ble_t *transport;
    esp_desktop_buddy_folder_push_t *folder_push;
    example_charpack_t *charpack;
    example_buddy_state_cache_t state_cache;
    char display_name[BUDDY_APP_NAME_MAX];
    char advertising_name[BUDDY_APP_BLE_NAME_MAX];
    char owner_name[BUDDY_APP_OWNER_MAX];
    char pack_status[BUDDY_APP_STATUS_MAX];
    bool have_active_pack;
    example_charpack_info_t active_pack;
    example_charpack_info_t installed_packs[BUDDY_APP_CHARPACK_LIST_MAX];
    size_t installed_pack_count;
    bool charpack_list_pending;
    esp_err_t charpack_list_result;
    bool charpack_switch_pending;
    esp_err_t charpack_switch_result;
    esp_desktop_buddy_transport_ble_state_t transport_state;
    uint32_t approval_count;
    uint32_t denial_count;
    uint32_t nap_seconds;
    example_progress_state_t progress;
    buddy_app_time_source_t time_source;
    int64_t time_last_synced_seconds;
    int32_t tz_offset_seconds;
    uint32_t aod_timeout_minutes;
    bool aod_enabled;
    bool settings_reset_pending;
    esp_err_t settings_reset_result;
    esp_err_t settings_migration_result;
    uint8_t display_brightness_percent;
    uint8_t aod_brightness_percent;
    buddy_app_rotation_mode_t rotation_mode;
    buddy_app_theme_t theme;
    /* 配置名称只在启动时应用，编辑不会中断当前链路；受 app->mutex 保护。 */
    char ble_device_name[BUDDY_APP_BLE_NAME_MAX];
    bool device_names_pending;
    esp_err_t device_names_result;

    buddy_app_activity_entry_t activity[BUDDY_APP_ACTIVITY_CAPACITY];
    uint32_t activity_next_sequence;
    uint8_t activity_next_index;
    uint8_t activity_count;
    QueueHandle_t prompt_reply_queue;
    SemaphoreHandle_t time_sync_mutex;
    char prompt_reply_queued_id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];
    char prompt_reply_inflight_id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];
    char prompt_reply_sent_id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];
    char prompt_reply_error_id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];
    uint32_t buddy_transport_session_generation;
    uint32_t prompt_reply_dropped_count;
    uint32_t prompt_reply_failed_count;
    TaskHandle_t ui_task;
    TaskHandle_t settings_task;
} buddy_app_t;

typedef struct {
    uint32_t available_caps;
    example_buddy_state_cache_t state_cache;
    bool prompt_reply_submitted;
    bool prompt_reply_failed;
    esp_desktop_buddy_transport_ble_state_t transport_state;
    example_charpack_info_t active_pack;
    bool have_active_pack;
    char pack_status[BUDDY_APP_STATUS_MAX];
    example_charpack_info_t installed_packs[BUDDY_APP_CHARPACK_LIST_MAX];
    size_t installed_pack_count;
    bool charpack_list_pending;
    esp_err_t charpack_list_result;
    bool charpack_switch_pending;
    esp_err_t charpack_switch_result;
    char advertising_name[BUDDY_APP_BLE_NAME_MAX];
    uint32_t aod_timeout_minutes;
    bool aod_enabled;
    bool settings_reset_pending;
    esp_err_t settings_reset_result;
    esp_err_t settings_migration_result;
    uint8_t display_brightness_percent;
    uint8_t aod_brightness_percent;
    buddy_app_rotation_mode_t rotation_mode;
    buddy_app_theme_t theme;
    /* 配置名称只在启动时应用，编辑不会中断当前链路；受 app->mutex 保护。 */
    char ble_device_name[BUDDY_APP_BLE_NAME_MAX];
    bool device_names_pending;
    esp_err_t device_names_result;

    buddy_app_time_source_t time_source;
    int64_t time_last_synced_seconds;
    int32_t tz_offset_seconds;
} buddy_app_ui_snapshot_t;

void buddy_app_init(buddy_app_t *app);

esp_desktop_buddy_status_reply_t buddy_app_status_handler(void *ctx, esp_desktop_buddy_t *buddy);
esp_desktop_buddy_command_result_t buddy_app_name_handler(void *ctx, esp_desktop_buddy_t *buddy, const char *name);
esp_desktop_buddy_command_result_t buddy_app_owner_handler(void *ctx, esp_desktop_buddy_t *buddy, const char *name);
esp_desktop_buddy_command_result_t buddy_app_unpair_handler(void *ctx, esp_desktop_buddy_t *buddy);
void buddy_app_buddy_event(void *ctx, const esp_desktop_buddy_event_t *event);
void buddy_app_transport_event(void *ctx, const esp_desktop_buddy_transport_ble_event_t *event);
void buddy_app_charpack_event(void *ctx, const example_charpack_event_t *event);
esp_err_t buddy_app_charpack_refresh_async(buddy_app_t *app);
esp_err_t buddy_app_charpack_set_active_async(buddy_app_t *app, const char *pack_id);

void buddy_app_print_state(buddy_app_t *app, FILE *out);
void buddy_app_diag_print(buddy_app_t *app, FILE *out);
void buddy_app_diag_snapshot_get(buddy_app_t *app, buddy_app_diag_snapshot_t *snapshot);
esp_desktop_buddy_command_extension_set_t buddy_app_command_extension(buddy_app_t *app);

esp_err_t buddy_app_ui_init(buddy_app_t *app);
void buddy_app_ui_start(buddy_app_t *app);
esp_err_t buddy_app_charpack_console_start(buddy_app_t *app);
esp_err_t buddy_app_time_apply_ble_sync(buddy_app_t *app,
                                        const esp_desktop_buddy_time_sync_t *time_sync);
void buddy_app_settings_restore(buddy_app_t *app);
void buddy_app_settings_request_save(buddy_app_t *app);
esp_err_t buddy_app_settings_set_device_name(buddy_app_t *app, bool bluetooth, const char *name);
esp_err_t buddy_app_settings_request_reset(buddy_app_t *app);
esp_err_t buddy_app_settings_reset(buddy_app_t *app);
esp_err_t buddy_app_activity_push_event(buddy_app_t *app,
                                        buddy_app_activity_source_t source,
                                        buddy_app_activity_level_t level,
                                        buddy_app_activity_event_t event,
                                        const char *parameter);
esp_err_t buddy_app_activity_push_generic(buddy_app_t *app,
                                          buddy_app_activity_source_t source,
                                          buddy_app_activity_level_t level,
                                          const char *safe_code);
size_t buddy_app_activity_snapshot_newest(buddy_app_t *app,
                                          buddy_app_activity_entry_t *entries,
                                          size_t capacity);
esp_err_t buddy_app_ui_events_init(buddy_app_t *app);
void buddy_app_ui_notify(const buddy_app_t *app, uint32_t dirty_bits);
uint32_t buddy_app_ui_wait_for_events(TickType_t timeout_ticks);
esp_err_t buddy_app_prompt_reply_enqueue(buddy_app_t *app,
                                         esp_desktop_buddy_permission_decision_t decision,
                                         const char *prompt_id);
void buddy_app_prompt_reply_process(buddy_app_t *app);
bool buddy_app_ui_snapshot_get(buddy_app_t *app, buddy_app_ui_snapshot_t *snapshot);
