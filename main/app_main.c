/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_pm.h"
#include "example_console.h"
#include "example_app_helpers.h"

#include "app_shared.h"
#include "ui_locale.h"

static const char *TAG = "esp32s3_buddy";
static buddy_app_t s_app;

static void buddy_app_set_capability(buddy_app_t *app, uint32_t capability, bool available)
{
    if (app == NULL || app->mutex == NULL) {
        return;
    }

    xSemaphoreTake(app->mutex, portMAX_DELAY);
    if (available) {
        app->available_caps |= capability;
    } else {
        app->available_caps &= ~capability;
    }
    xSemaphoreGive(app->mutex);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
}

static void buddy_app_detach_ble_state(buddy_app_t *app)
{
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->transport = NULL;
    app->buddy = NULL;
    app->available_caps &= ~BUDDY_APP_CAP_BLE;
    xSemaphoreGive(app->mutex);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
}

static void buddy_app_set_pack_unavailable(buddy_app_t *app, const char *message)
{
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    app->available_caps &= ~BUDDY_APP_CAP_STORAGE;
    app->charpack = NULL;
    app->folder_push = NULL;
    app->have_active_pack = false;
    app->installed_pack_count = 0;
    strlcpy(app->pack_status, message, sizeof(app->pack_status));
    xSemaphoreGive(app->mutex);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_BUDDY);
}


static void buddy_app_configure_power(void)
{
#if CONFIG_PM_ENABLE
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&pm_config);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "power management config failed: %s", esp_err_to_name(err));
    }
#endif
}

void app_main(void)
{
    esp_err_t err;
    example_charpack_t *charpack = NULL;
    esp_desktop_buddy_folder_push_t *folder_push = NULL;
    esp_desktop_buddy_t *buddy = NULL;
    esp_desktop_buddy_transport_ble_t *transport = NULL;
    char advertising_name[BUDDY_APP_BLE_NAME_MAX] = {0};
    esp_desktop_buddy_config_t buddy_config = {
        .event_sink = {
            .on_event = buddy_app_buddy_event,
            .ctx = &s_app,
        },
        .handlers = {
            .ctx = &s_app,
            .on_status = buddy_app_status_handler,
            .on_name = buddy_app_name_handler,
            .on_owner = buddy_app_owner_handler,
            .on_unpair = buddy_app_unpair_handler,
        },
    };
    esp_desktop_buddy_transport_ble_config_t transport_config = {
        .security = {
            .bonding = true,
            .mitm = true,
            .secure_connections = true,
            .io_capability = ESP_DESKTOP_BUDDY_TRANSPORT_BLE_IO_CAP_DISPLAY_ONLY,
        },
        .on_event = buddy_app_transport_event,
        .event_ctx = &s_app,
    };
    example_charpack_config_t charpack_config = {
        .mount_point = NULL,
        .packs_root = NULL,
        .staging_root = NULL,
        .format_if_mount_failed = false,
        .on_event = buddy_app_charpack_event,
        .event_ctx = &s_app,
    };
    esp_desktop_buddy_folder_push_config_t folder_push_config = {0};

    bsp_memory_log_stage("boot");
    ESP_ERROR_CHECK(example_init_nvs());
    ui_locale_init();

    buddy_app_init(&s_app);
    s_app.mutex = xSemaphoreCreateMutex();
    s_app.time_sync_mutex = xSemaphoreCreateMutex();
    if (s_app.mutex == NULL || s_app.time_sync_mutex == NULL) {
        abort();
    }
    ESP_ERROR_CHECK(buddy_app_ui_events_init(&s_app));
    buddy_app_settings_restore(&s_app);
    transport_config.advertising_name_override = s_app.ble_device_name;

    example_restore_persisted_string("buddy",
                                       "display_name",
                                       s_app.display_name,
                                       sizeof(s_app.display_name),
                                       "ESP32-S3 Buddy");

    err = example_charpack_new(&charpack_config, &charpack);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "character storage unavailable: %s", esp_err_to_name(err));
        buddy_app_set_pack_unavailable(&s_app, "Character storage unavailable");
    } else {
        example_charpack_info_t active_pack = {0};
        bool have_active = example_charpack_get_active(charpack, &active_pack) == ESP_OK;

        xSemaphoreTake(s_app.mutex, portMAX_DELAY);
        s_app.charpack = charpack;
        s_app.available_caps |= BUDDY_APP_CAP_STORAGE;
        if (have_active) {
            s_app.active_pack = active_pack;
            s_app.have_active_pack = true;
            example_safe_copy(s_app.pack_status,
                              sizeof(s_app.pack_status),
                              "Active pack loaded");
        }
        xSemaphoreGive(s_app.mutex);
        buddy_app_ui_notify(&s_app, BUDDY_APP_UI_DIRTY_BUDDY);

        if (buddy_app_charpack_refresh_async(&s_app) != ESP_OK) {
            ESP_LOGW(TAG, "character pack list prefetch unavailable");
        }

        const esp_desktop_buddy_folder_push_sink_t *sink = example_charpack_get_sink(charpack);
        if (sink == NULL) {
            ESP_LOGW(TAG, "folder push unavailable: character storage sink missing");
        } else {
            folder_push_config.sink = *sink;
            err = esp_desktop_buddy_folder_push_new(&folder_push_config, &folder_push);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "folder push unavailable: %s", esp_err_to_name(err));
            } else {
                xSemaphoreTake(s_app.mutex, portMAX_DELAY);
                s_app.folder_push = folder_push;
                xSemaphoreGive(s_app.mutex);
            }
        }
    }

    buddy_config.handlers.command_extension = buddy_app_command_extension(&s_app);
    err = esp_desktop_buddy_new(&buddy_config, &buddy);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Buddy unavailable: %s; BLE skipped", esp_err_to_name(err));
        buddy_app_set_capability(&s_app, BUDDY_APP_CAP_BLE, false);
    } else {
        xSemaphoreTake(s_app.mutex, portMAX_DELAY);
        s_app.buddy = buddy;
        xSemaphoreGive(s_app.mutex);
        transport_config.buddy = buddy;
        err = esp_desktop_buddy_transport_ble_new(&transport_config, &transport);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "BLE unavailable: %s", esp_err_to_name(err));
            buddy_app_detach_ble_state(&s_app);
            (void)esp_desktop_buddy_delete(buddy);
            buddy = NULL;
        } else {
            err = esp_desktop_buddy_transport_ble_get_advertising_name(
                transport, advertising_name, sizeof(advertising_name));
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "BLE advertising name unavailable: %s; transport remains available",
                         esp_err_to_name(err));
                strlcpy(advertising_name, "BLE available", sizeof(advertising_name));
            }
            xSemaphoreTake(s_app.mutex, portMAX_DELAY);
            s_app.transport = transport;
            if (err == ESP_OK) {
                strlcpy(s_app.advertising_name,
                        advertising_name,
                        sizeof(s_app.advertising_name));
            }
            s_app.available_caps |= BUDDY_APP_CAP_BLE;
            xSemaphoreGive(s_app.mutex);
            buddy_app_ui_notify(&s_app, BUDDY_APP_UI_DIRTY_BUDDY);
        }
    }

    bsp_memory_log_stage("BLE init");
    example_console_init();
    /* 存储故障时仍保留诊断和显式恢复入口。 */
    esp_err_t console_err = buddy_app_charpack_console_start(&s_app);
    if (console_err != ESP_OK) {
        ESP_LOGW(TAG, "continuing without serial console");
    }
    buddy_app_set_capability(&s_app, BUDDY_APP_CAP_TIME, true);
    buddy_app_configure_power();
    ESP_ERROR_CHECK(buddy_app_ui_init(&s_app));
    buddy_app_ui_start(&s_app);
    ESP_LOGI(TAG,
             "ready: ESP32-S3 Buddy (advertising as %s)",
             advertising_name[0] != '\0' ? advertising_name : "BLE unavailable");
}
