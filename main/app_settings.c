/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "app_shared.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ui_locale.h"

#define BUDDY_APP_SETTINGS_NAMESPACE "buddy_cfg"
#define BUDDY_APP_SETTINGS_SCHEMA_KEY "schema"
#define BUDDY_APP_SETTINGS_SCHEMA_VERSION 3U
#define BUDDY_APP_WIFI_NAMESPACE "buddy_net"
#define BUDDY_APP_WIFI_SCHEMA_KEY "schema"
#define BUDDY_APP_WIFI_SCHEMA_VERSION 2U
#define BUDDY_APP_WEATHER_NAMESPACE "weather"
#define BUDDY_APP_WEATHER_SCHEMA_KEY "schema"
#define BUDDY_APP_WEATHER_SCHEMA_VERSION 1U
#define BUDDY_APP_RESET_NAMESPACE "buddy_reset"
#define BUDDY_APP_RESET_PENDING_KEY "pending"
#define BUDDY_APP_AOD_DEFAULT_MINUTES 30
#define BUDDY_APP_AOD_MIN_MINUTES 1
#define BUDDY_APP_AOD_MAX_MINUTES 180
#define BUDDY_APP_DISPLAY_BRIGHTNESS_DEFAULT 80
#define BUDDY_APP_AOD_BRIGHTNESS_DEFAULT 8
#define BUDDY_APP_SETTINGS_STACK 3072
#define BUDDY_APP_SETTINGS_PRIORITY 2
#define BUDDY_APP_SETTINGS_SAVE_DELAY_MS 1500

#if CONFIG_FREERTOS_UNICORE
#define BUDDY_APP_SETTINGS_CORE 0
#else
#define BUDDY_APP_SETTINGS_CORE 1
#endif

typedef enum {
    BUDDY_APP_SETTINGS_REQUEST_SAVE = 0,
    BUDDY_APP_SETTINGS_REQUEST_RESET,
} buddy_app_settings_request_kind_t;

typedef struct {
    uint32_t aod_timeout_minutes;
    bool aod_enabled;
    uint8_t display_brightness_percent;
    uint8_t aod_brightness_percent;
    ui_locale_t locale;
    buddy_app_rotation_mode_t rotation_mode;
    buddy_app_theme_t theme;
    char ble_device_name[BUDDY_APP_BLE_NAME_MAX];
} buddy_app_settings_values_t;

typedef struct {
    buddy_app_settings_request_kind_t kind;
    uint32_t generation;
    buddy_app_t *app;
    buddy_app_settings_values_t values;
} buddy_app_settings_request_t;

static const char *TAG = "buddy_app_settings";
static QueueHandle_t s_settings_queue;
static TaskHandle_t s_settings_task;
static SemaphoreHandle_t s_settings_request_mutex;
static portMUX_TYPE s_settings_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_settings_generation;

/* 输入键盘提供 ASCII。禁止空名称、控制字符和首尾空格，不静默截断。 */
static bool buddy_app_device_name_valid(const char *name)
{
    if (name == NULL) return false;
    size_t length = strnlen(name, BUDDY_APP_BLE_NAME_MAX);
    if (length == 0 || length > (BUDDY_APP_BLE_NAME_MAX - 1) ||
        name[0] == ' ' || name[length - 1] == ' ') return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 32 || ch > 126) return false;
    }
    return true;
}

static void buddy_app_device_names_defaults(buddy_app_settings_values_t *values)
{
    esp_desktop_buddy_transport_ble_build_default_name(values->ble_device_name,
                                                       sizeof(values->ble_device_name));
}


static uint32_t buddy_app_clamp_aod_timeout(uint32_t value)
{
    if (value < BUDDY_APP_AOD_MIN_MINUTES) {
        return BUDDY_APP_AOD_MIN_MINUTES;
    }
    return value > BUDDY_APP_AOD_MAX_MINUTES ? BUDDY_APP_AOD_MAX_MINUTES : value;
}

static uint8_t buddy_app_clamp_brightness(uint8_t value)
{
    return value > 100U ? 100U : value;
}


static buddy_app_rotation_mode_t buddy_app_validate_rotation_mode(uint8_t value)
{
    return value >= BUDDY_APP_ROTATION_PORTRAIT && value < BUDDY_APP_ROTATION_MODE_COUNT ?
           (buddy_app_rotation_mode_t)value : BUDDY_APP_ROTATION_PORTRAIT;
}


static uint32_t buddy_app_settings_generation_get(void)
{
    uint32_t generation;

    portENTER_CRITICAL(&s_settings_lock);
    generation = s_settings_generation;
    portEXIT_CRITICAL(&s_settings_lock);
    return generation;
}

static uint32_t buddy_app_settings_generation_advance(void)
{
    uint32_t generation;

    portENTER_CRITICAL(&s_settings_lock);
    generation = ++s_settings_generation;
    portEXIT_CRITICAL(&s_settings_lock);
    return generation;
}

static esp_err_t buddy_app_settings_schema_supported(const char *namespace_name,
                                                     const char *schema_key,
                                                     uint8_t current_version)
{
    nvs_handle_t nvs;
    uint8_t schema = 0;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u8(nvs, schema_key, &schema);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (schema > current_version) {
        ESP_LOGW(TAG, "%s schema %u is newer than supported %u",
                 namespace_name, (unsigned)schema, (unsigned)current_version);
        return ESP_ERR_INVALID_VERSION;
    }
    return ESP_OK;
}

/* 只清理已知旧 namespace；全部版本检查先于任何删除。
 * 跨 namespace 不能原子提交，schema 最后写入，使中断后的重试幂等。 */
static esp_err_t buddy_app_settings_erase_namespace(const char *name)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(name, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(nvs);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static esp_err_t buddy_app_settings_migrate(void)
{
    esp_err_t err = buddy_app_settings_schema_supported(BUDDY_APP_SETTINGS_NAMESPACE,
                                                        BUDDY_APP_SETTINGS_SCHEMA_KEY, 3);
    if (err == ESP_OK) err = buddy_app_settings_schema_supported(BUDDY_APP_WIFI_NAMESPACE, "schema", 2);
    if (err == ESP_OK) err = buddy_app_settings_schema_supported(BUDDY_APP_WEATHER_NAMESPACE, "schema", 1);
    if (err != ESP_OK) return err;
    nvs_handle_t nvs;
    uint8_t schema = 0;
    err = nvs_open(BUDDY_APP_SETTINGS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_get_u8(nvs, "schema", &schema);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    nvs_close(nvs);
    if (err != ESP_OK || schema == BUDDY_APP_SETTINGS_SCHEMA_VERSION) return err;
    err = buddy_app_settings_erase_namespace(BUDDY_APP_WIFI_NAMESPACE);
    if (err == ESP_OK) err = buddy_app_settings_erase_namespace(BUDDY_APP_WEATHER_NAMESPACE);
    if (err != ESP_OK) return err;
    err = nvs_open(BUDDY_APP_SETTINGS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    const char *keys[] = {"thresholds", "tz_offset", "wifi_host"};
    for (size_t i = 0; err == ESP_OK && i < sizeof(keys) / sizeof(keys[0]); ++i) {
        err = nvs_erase_key(nvs, keys[i]);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    uint8_t rotation = BUDDY_APP_ROTATION_PORTRAIT;
    if (err == ESP_OK) {
        esp_err_t read_err = nvs_get_u8(nvs, "rotation", &rotation);
        if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) err = read_err;
    }
    if (err == ESP_OK) err = nvs_set_u8(nvs, "rotation", buddy_app_validate_rotation_mode(rotation));
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "schema", BUDDY_APP_SETTINGS_SCHEMA_VERSION);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static void buddy_app_settings_capture(buddy_app_t *app, buddy_app_settings_values_t *values)
{
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    values->aod_timeout_minutes = app->aod_timeout_minutes;
    values->aod_enabled = app->aod_enabled;
    values->display_brightness_percent = app->display_brightness_percent;
    values->aod_brightness_percent = app->aod_brightness_percent;
    values->rotation_mode = app->rotation_mode;
    values->theme = app->theme;
    strlcpy(values->ble_device_name, app->ble_device_name, sizeof(values->ble_device_name));
    xSemaphoreGive(app->mutex);
    values->aod_timeout_minutes = buddy_app_clamp_aod_timeout(values->aod_timeout_minutes);
    values->display_brightness_percent = buddy_app_clamp_brightness(values->display_brightness_percent);
    values->aod_brightness_percent = buddy_app_clamp_brightness(values->aod_brightness_percent);
    values->rotation_mode = buddy_app_validate_rotation_mode((uint8_t)values->rotation_mode);
    values->theme = values->theme < BUDDY_APP_THEME_COUNT ? values->theme : BUDDY_APP_THEME_DEEPSEEK;
    values->locale = ui_locale_get();
}

static esp_err_t buddy_app_settings_write(const buddy_app_settings_values_t *values)
{
    nvs_handle_t nvs = 0;
    uint8_t schema = 0;
    esp_err_t err = buddy_app_settings_migrate();
    if (err != ESP_OK) return err;
    err = nvs_open(BUDDY_APP_SETTINGS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    if (nvs_get_u8(nvs, BUDDY_APP_SETTINGS_SCHEMA_KEY, &schema) == ESP_OK &&
        schema > BUDDY_APP_SETTINGS_SCHEMA_VERSION) {
        nvs_close(nvs);
        ESP_LOGW(TAG, "settings schema %u is newer than supported %u",
                 (unsigned)schema, (unsigned)BUDDY_APP_SETTINGS_SCHEMA_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }
    err = nvs_set_u32(nvs, "aod_min", values->aod_timeout_minutes);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "aod_en", values->aod_enabled ? 1U : 0U);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "disp_pct", values->display_brightness_percent);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "aod_pct", values->aod_brightness_percent);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "rotation", (uint8_t)values->rotation_mode);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "theme", (uint8_t)values->theme);
    if (err == ESP_OK) err = nvs_set_str(nvs, "ble_name", values->ble_device_name);
    if (err == ESP_OK) err = nvs_set_u8(nvs, BUDDY_APP_SETTINGS_SCHEMA_KEY, BUDDY_APP_SETTINGS_SCHEMA_VERSION);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) {
        err = ui_locale_save(values->locale);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings save failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t buddy_app_settings_reset_marker_read(bool *pending)
{
    nvs_handle_t nvs = 0;
    uint8_t value = 0;
    esp_err_t err;

    if (pending == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *pending = false;
    err = nvs_open(BUDDY_APP_RESET_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u8(nvs, BUDDY_APP_RESET_PENDING_KEY, &value);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK) {
        *pending = value != 0U;
    }
    return err;
}

static esp_err_t buddy_app_settings_reset_marker_write(bool pending)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(BUDDY_APP_RESET_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK) {
        return err;
    }
    if (pending) {
        err = nvs_set_u8(nvs, BUDDY_APP_RESET_PENDING_KEY, 1U);
    } else {
        err = nvs_erase_key(nvs, BUDDY_APP_RESET_PENDING_KEY);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t buddy_app_settings_perform_reset(buddy_app_t *app)
{
    buddy_app_settings_values_t values = {
        .aod_timeout_minutes = BUDDY_APP_AOD_DEFAULT_MINUTES,
        .aod_enabled = true,
        .display_brightness_percent = BUDDY_APP_DISPLAY_BRIGHTNESS_DEFAULT,
        .aod_brightness_percent = BUDDY_APP_AOD_BRIGHTNESS_DEFAULT,
        .rotation_mode = BUDDY_APP_ROTATION_PORTRAIT,
    };
    buddy_app_device_names_defaults(&values);
    nvs_handle_t nvs = 0;
    esp_err_t result = buddy_app_settings_schema_supported(BUDDY_APP_SETTINGS_NAMESPACE,
                                                            BUDDY_APP_SETTINGS_SCHEMA_KEY,
                                                            BUDDY_APP_SETTINGS_SCHEMA_VERSION);

    if (result == ESP_OK) {
        result = buddy_app_settings_schema_supported(BUDDY_APP_WIFI_NAMESPACE,
                                                     BUDDY_APP_WIFI_SCHEMA_KEY,
                                                     BUDDY_APP_WIFI_SCHEMA_VERSION);
    }
    if (result == ESP_OK) {
        result = buddy_app_settings_schema_supported(BUDDY_APP_WEATHER_NAMESPACE,
                                                     BUDDY_APP_WEATHER_SCHEMA_KEY,
                                                     BUDDY_APP_WEATHER_SCHEMA_VERSION);
    }
    if (result != ESP_OK) {
        return result;
    }

    result = buddy_app_settings_reset_marker_write(true);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_open(BUDDY_APP_SETTINGS_NAMESPACE, NVS_READWRITE, &nvs);

    if (result == ESP_OK) {
        result = nvs_erase_all(nvs);
        if (result == ESP_OK) result = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    if (result == ESP_OK) result = ui_locale_clear_saved();
    if (result == ESP_OK) result = buddy_app_settings_erase_namespace(BUDDY_APP_WIFI_NAMESPACE);
    if (result == ESP_OK) result = buddy_app_settings_erase_namespace(BUDDY_APP_WEATHER_NAMESPACE);
    if (result == ESP_OK) result = bsp_touch_reset_calibration();
    if (result != ESP_OK) return result;

    ui_locale_reset();
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->aod_timeout_minutes = values.aod_timeout_minutes;
    app->aod_enabled = values.aod_enabled;
    app->display_brightness_percent = values.display_brightness_percent;
    app->aod_brightness_percent = values.aod_brightness_percent;
    app->rotation_mode = values.rotation_mode;
    app->theme = values.theme;
    strlcpy(app->ble_device_name, values.ble_device_name, sizeof(app->ble_device_name));
    app->device_names_pending = false;
    app->device_names_result = ESP_ERR_INVALID_STATE;
    xSemaphoreGive(app->mutex);
    result = buddy_app_settings_write(&values);
    if (result != ESP_OK) return result;
    return buddy_app_settings_reset_marker_write(false);
}

static void buddy_app_settings_finish_reset(buddy_app_t *app, esp_err_t result)
{
    bool persistent_pending = false;

    if (result != ESP_OK &&
        buddy_app_settings_reset_marker_read(&persistent_pending) != ESP_OK) {
        /* Unknown marker state must block later saves until a reboot can retry recovery. */
        persistent_pending = true;
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->settings_reset_pending = persistent_pending;
    app->settings_reset_result = result;
    if (result == ESP_OK) app->settings_migration_result = ESP_OK;
    xSemaphoreGive(app->mutex);
    if (result == ESP_OK) {
        (void)buddy_app_activity_push_event(app, BUDDY_APP_ACTIVITY_SOURCE_SYSTEM,
                                            BUDDY_APP_ACTIVITY_LEVEL_INFO,
                                            BUDDY_APP_ACTIVITY_EVENT_SETTINGS_RESET, NULL);
    } else {
        (void)buddy_app_activity_push_generic(app, BUDDY_APP_ACTIVITY_SOURCE_SYSTEM,
                                              BUDDY_APP_ACTIVITY_LEVEL_ERROR, "settings_reset_failed");
    }
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_SETTINGS | BUDDY_APP_UI_DIRTY_TIME |
                             BUDDY_APP_UI_DIRTY_ACTIVITY | BUDDY_APP_UI_DIRTY_LAYOUT);
}

static void buddy_app_device_names_saved(const buddy_app_settings_request_t *request, esp_err_t result)
{
    xSemaphoreTake(request->app->mutex, portMAX_DELAY);
    if (result == ESP_OK) request->app->settings_migration_result = ESP_OK;
    /* 延迟保存被新编辑替换时，旧结果不得确认更新后的名称。 */
    if (request->app->device_names_pending && !request->app->settings_reset_pending &&
        request->generation == buddy_app_settings_generation_get() &&
        strcmp(request->values.ble_device_name, request->app->ble_device_name) == 0) {
        request->app->device_names_pending = false;
        request->app->device_names_result = result;
    }
    xSemaphoreGive(request->app->mutex);
    buddy_app_ui_notify(request->app, BUDDY_APP_UI_DIRTY_SETTINGS);
}

static void buddy_app_settings_task(void *arg)
{
    buddy_app_settings_request_t request;
    buddy_app_settings_request_t next;

    (void)arg;
    while (true) {
        if (xQueueReceive(s_settings_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        if (request.kind == BUDDY_APP_SETTINGS_REQUEST_SAVE) {
            while (xQueueReceive(s_settings_queue, &next,
                                 pdMS_TO_TICKS(BUDDY_APP_SETTINGS_SAVE_DELAY_MS)) == pdTRUE) {
                request = next;
                if (request.kind == BUDDY_APP_SETTINGS_REQUEST_RESET) break;
            }
        }
        if (request.kind == BUDDY_APP_SETTINGS_REQUEST_RESET) {
            esp_err_t result = buddy_app_settings_perform_reset(request.app);
            bool reset_started = false;
            esp_err_t marker_err;
            esp_err_t saved_result = ESP_ERR_INVALID_STATE;
            bool did_save = false;

            marker_err = buddy_app_settings_reset_marker_read(&reset_started);
            if (result != ESP_OK && marker_err == ESP_OK && !reset_started) {
                /* Reset preflight/runtime failures must not discard the latest delayed save. */
                saved_result = buddy_app_settings_write(&request.values);
                did_save = true;
            }
            buddy_app_settings_finish_reset(request.app, result);
            if (did_save) buddy_app_device_names_saved(&request, saved_result);
            else if (result != ESP_OK) {
                xSemaphoreTake(request.app->mutex, portMAX_DELAY);
                request.app->device_names_pending = false;
                request.app->device_names_result = result;
                xSemaphoreGive(request.app->mutex);
                buddy_app_ui_notify(request.app, BUDDY_APP_UI_DIRTY_SETTINGS);
            }
        } else if (request.generation == buddy_app_settings_generation_get()) {
            esp_err_t result = buddy_app_settings_write(&request.values);
            buddy_app_device_names_saved(&request, result);
        }
    }
}

static void buddy_app_settings_start_task(void)
{
    if (s_settings_task != NULL) return;
    if (s_settings_request_mutex == NULL) {
        s_settings_request_mutex = xSemaphoreCreateMutex();
        if (s_settings_request_mutex == NULL) {
            ESP_LOGW(TAG, "failed to create settings request mutex");
            return;
        }
    }
    s_settings_queue = xQueueCreate(1, sizeof(buddy_app_settings_request_t));
    if (s_settings_queue == NULL) {
        ESP_LOGW(TAG, "failed to create settings queue");
        return;
    }
    if (xTaskCreatePinnedToCore(buddy_app_settings_task, "buddy_app_settings",
                                BUDDY_APP_SETTINGS_STACK, NULL, BUDDY_APP_SETTINGS_PRIORITY,
                                &s_settings_task, BUDDY_APP_SETTINGS_CORE) != pdPASS) {
        vQueueDelete(s_settings_queue);
        s_settings_queue = NULL;
        vSemaphoreDelete(s_settings_request_mutex);
        s_settings_request_mutex = NULL;
        ESP_LOGW(TAG, "failed to start settings task");
    }
}

void buddy_app_settings_restore(buddy_app_t *app)
{
    buddy_app_settings_values_t values = {
        .aod_timeout_minutes = BUDDY_APP_AOD_DEFAULT_MINUTES, .aod_enabled = true,
        .display_brightness_percent = BUDDY_APP_DISPLAY_BRIGHTNESS_DEFAULT,
        .aod_brightness_percent = BUDDY_APP_AOD_BRIGHTNESS_DEFAULT,
        .rotation_mode = BUDDY_APP_ROTATION_PORTRAIT,
    };
    nvs_handle_t nvs = 0;
    uint8_t schema = 0;
    uint8_t aod_enabled = 1U;
    uint8_t rotation_mode = BUDDY_APP_ROTATION_PORTRAIT;
    uint8_t theme = BUDDY_APP_THEME_DEEPSEEK;
    bool reset_pending = false;
    esp_err_t reset_err;

    if (app == NULL || app->mutex == NULL) return;
    buddy_app_device_names_defaults(&values);
    reset_err = buddy_app_settings_reset_marker_read(&reset_pending);
    if (reset_err == ESP_OK && reset_pending) {
        reset_err = buddy_app_settings_perform_reset(app);
        if (reset_err == ESP_OK) {
            reset_pending = false;
            ESP_LOGW(TAG, "completed interrupted settings reset");
        } else {
            ESP_LOGE(TAG, "interrupted settings reset remains pending: %s",
                     esp_err_to_name(reset_err));
        }
    } else if (reset_err != ESP_OK) {
        reset_pending = true;
        ESP_LOGW(TAG, "settings reset marker unavailable: %s", esp_err_to_name(reset_err));
    }
    esp_err_t migration_err = reset_pending ? reset_err : buddy_app_settings_migrate();
    if (migration_err != ESP_OK) ESP_LOGE(TAG, "settings migration failed: %s", esp_err_to_name(migration_err));
    app->settings_migration_result = migration_err;
    /* 清理失败不能让已保存的保留设置变成默认值，进而在重试保存时被覆盖。 */
    esp_err_t retained_schema = buddy_app_settings_schema_supported(BUDDY_APP_SETTINGS_NAMESPACE,
                                                                    BUDDY_APP_SETTINGS_SCHEMA_KEY, 3);
    if (!reset_pending && retained_schema == ESP_OK && nvs_open(BUDDY_APP_SETTINGS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        (void)nvs_get_u8(nvs, BUDDY_APP_SETTINGS_SCHEMA_KEY, &schema);
        (void)nvs_get_u32(nvs, "aod_min", &values.aod_timeout_minutes);
        if (nvs_get_u8(nvs, "aod_en", &aod_enabled) == ESP_OK) values.aod_enabled = aod_enabled != 0U;
        (void)nvs_get_u8(nvs, "disp_pct", &values.display_brightness_percent);
        (void)nvs_get_u8(nvs, "aod_pct", &values.aod_brightness_percent);
        if (nvs_get_u8(nvs, "theme", &theme) == ESP_OK && theme < BUDDY_APP_THEME_COUNT) {
            values.theme = (buddy_app_theme_t)theme;
        }
        if (nvs_get_u8(nvs, "rotation", &rotation_mode) == ESP_OK) {
            values.rotation_mode = buddy_app_validate_rotation_mode(rotation_mode);
        }
        char name[33];
        size_t name_size = sizeof(name);
        if (nvs_get_str(nvs, "ble_name", name, &name_size) == ESP_OK &&
            buddy_app_device_name_valid(name))
            strlcpy(values.ble_device_name, name, sizeof(values.ble_device_name));
        nvs_close(nvs);
    }
    values.aod_timeout_minutes = buddy_app_clamp_aod_timeout(values.aod_timeout_minutes);
    values.display_brightness_percent = buddy_app_clamp_brightness(values.display_brightness_percent);
    values.aod_brightness_percent = buddy_app_clamp_brightness(values.aod_brightness_percent);
    ui_locale_init();
    values.locale = ui_locale_get();
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->aod_timeout_minutes = values.aod_timeout_minutes;
    app->aod_enabled = values.aod_enabled;
    app->display_brightness_percent = values.display_brightness_percent;
    app->aod_brightness_percent = values.aod_brightness_percent;
    app->rotation_mode = values.rotation_mode;
    app->theme = values.theme;
    strlcpy(app->ble_device_name, values.ble_device_name, sizeof(app->ble_device_name));
    app->device_names_pending = false;
    app->device_names_result = ESP_ERR_INVALID_STATE;
    app->settings_reset_pending = reset_pending;
    app->settings_reset_result = reset_pending ? reset_err : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(app->mutex);
    buddy_app_settings_start_task();
    app->settings_task = s_settings_task;
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_SETTINGS | BUDDY_APP_UI_DIRTY_TIME);
    if (!reset_pending && migration_err == ESP_OK && schema < BUDDY_APP_SETTINGS_SCHEMA_VERSION) (void)buddy_app_settings_write(&values);
    else if (schema > BUDDY_APP_SETTINGS_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "settings schema %u is newer than supported %u",
                 (unsigned)schema, (unsigned)BUDDY_APP_SETTINGS_SCHEMA_VERSION);
    }
}

void buddy_app_settings_request_save(buddy_app_t *app)
{
    buddy_app_settings_request_t request = {.kind = BUDDY_APP_SETTINGS_REQUEST_SAVE, .app = app};
    bool reset_pending;

    if (app == NULL || app->mutex == NULL) return;
    buddy_app_settings_start_task();
    if (s_settings_queue == NULL || s_settings_request_mutex == NULL) {
        ESP_LOGW(TAG, "settings save queue unavailable");
        return;
    }
    xSemaphoreTake(s_settings_request_mutex, portMAX_DELAY);
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    reset_pending = app->settings_reset_pending;
    xSemaphoreGive(app->mutex);
    if (!reset_pending) {
        request.generation = buddy_app_settings_generation_get();
        buddy_app_settings_capture(app, &request.values);
    }
    if (!reset_pending && xQueueOverwrite(s_settings_queue, &request) != pdTRUE) {
        ESP_LOGW(TAG, "settings save queue unavailable");
    }
    xSemaphoreGive(s_settings_request_mutex);
}

esp_err_t buddy_app_settings_set_device_name(buddy_app_t *app, bool bluetooth, const char *name)
{
    buddy_app_settings_request_t request = {.kind = BUDDY_APP_SETTINGS_REQUEST_SAVE, .app = app};
    if (app == NULL || app->mutex == NULL || !bluetooth || !buddy_app_device_name_valid(name))
        return ESP_ERR_INVALID_ARG;
    if (s_settings_queue == NULL || s_settings_request_mutex == NULL)
        return ESP_ERR_INVALID_STATE;
    /* 与保存／恢复默认共用请求锁，避免重置请求被名称编辑覆盖。 */
    xSemaphoreTake(s_settings_request_mutex, portMAX_DELAY);
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    if (app->settings_reset_pending) {
        xSemaphoreGive(app->mutex);
        xSemaphoreGive(s_settings_request_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    char old_name[33];
    char *target = app->ble_device_name;
    size_t capacity = sizeof(app->ble_device_name);
    strlcpy(old_name, target, sizeof(old_name));
    strlcpy(target, name, capacity);
    xSemaphoreGive(app->mutex);
    request.generation = buddy_app_settings_generation_get();
    buddy_app_settings_capture(app, &request.values);
    /* 先发布 pending 再投递，任务完成后才显示已保存。 */
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->device_names_pending = true;
    app->device_names_result = ESP_ERR_INVALID_STATE;
    esp_err_t result = xQueueOverwrite(s_settings_queue, &request) == pdTRUE ? ESP_OK : ESP_FAIL;
    if (result != ESP_OK) {
        strlcpy(target, old_name, capacity);
        app->device_names_pending = false;
        app->device_names_result = result;
    }
    xSemaphoreGive(app->mutex);
    xSemaphoreGive(s_settings_request_mutex);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_SETTINGS);
    return result;
}


esp_err_t buddy_app_settings_request_reset(buddy_app_t *app)
{
    buddy_app_settings_request_t request = {.kind = BUDDY_APP_SETTINGS_REQUEST_RESET, .app = app};
    bool reset_pending;

    if (app == NULL || app->mutex == NULL) return ESP_ERR_INVALID_ARG;
    buddy_app_settings_start_task();
    if (s_settings_queue == NULL || s_settings_request_mutex == NULL) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_settings_request_mutex, portMAX_DELAY);
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    reset_pending = app->settings_reset_pending;
    xSemaphoreGive(app->mutex);
    if (reset_pending) {
        xSemaphoreGive(s_settings_request_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    buddy_app_settings_capture(app, &request.values);
    request.generation = buddy_app_settings_generation_advance();
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->settings_reset_pending = true;
    app->settings_reset_result = ESP_ERR_INVALID_STATE;
    xSemaphoreGive(app->mutex);
    if (xQueueOverwrite(s_settings_queue, &request) != pdTRUE) {
        xSemaphoreTake(app->mutex, portMAX_DELAY);
        app->settings_reset_pending = false;
        app->settings_reset_result = ESP_FAIL;
        xSemaphoreGive(app->mutex);
        xSemaphoreGive(s_settings_request_mutex);
        return ESP_FAIL;
    }
    xSemaphoreGive(s_settings_request_mutex);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_SETTINGS);
    return ESP_OK;
}

esp_err_t buddy_app_settings_reset(buddy_app_t *app)
{
    return buddy_app_settings_request_reset(app);
}
