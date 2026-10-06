/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "app_shared.h"

#include "esp_err.h"

static esp_desktop_buddy_command_extension_result_t buddy_app_ack(bool ok, const char *error)
{
    return (esp_desktop_buddy_command_extension_result_t){
        .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
        .reply = {
            .ok = ok,
            .n = 0,
            .error = error,
        },
    };
}


static bool buddy_app_ble_is_encrypted(buddy_app_t *app)
{
    bool encrypted;

    if (app == NULL || app->mutex == NULL) {
        return false;
    }
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    encrypted = app->transport_state.encrypted;
    xSemaphoreGive(app->mutex);
    return encrypted;
}


/* 旧主机请求明确失败；不解析或保存凭据。 */
static esp_desktop_buddy_command_extension_result_t buddy_app_removed_command(
    void *ctx, esp_desktop_buddy_t *buddy, const esp_desktop_buddy_command_view_t *request)
{
    (void)ctx; (void)buddy; (void)request;
    return buddy_app_ack(false, "unsupported");
}

static esp_desktop_buddy_command_extension_result_t buddy_app_folder_command(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request)
{
    buddy_app_t *app = (buddy_app_t *)ctx;

    if (!buddy_app_ble_is_encrypted(app)) {
        return buddy_app_ack(false, "ble_not_encrypted");
    }
    if (app->folder_push == NULL) {
        return buddy_app_ack(false, "folder_push_unavailable");
    }
    return esp_desktop_buddy_folder_push_handle_command(app->folder_push, buddy, request);
}

esp_desktop_buddy_command_extension_set_t buddy_app_command_extension(buddy_app_t *app)
{
    static const esp_desktop_buddy_command_extension_entry_t s_bindings[] = {
        { .command = "wifi_set", .handler = buddy_app_removed_command },
        { .command = "weather_key", .handler = buddy_app_removed_command },
        { .command = "weather_loc", .handler = buddy_app_removed_command },
        { .command = "char_begin", .handler = buddy_app_folder_command },
        { .command = "file", .handler = buddy_app_folder_command },
        { .command = "chunk", .handler = buddy_app_folder_command },
        { .command = "file_end", .handler = buddy_app_folder_command },
        { .command = "char_end", .handler = buddy_app_folder_command },
        { .command = "char_abort", .handler = buddy_app_folder_command },
    };

    return (esp_desktop_buddy_command_extension_set_t){
        .ctx = app,
        .bindings = s_bindings,
        .binding_count = sizeof(s_bindings) / sizeof(s_bindings[0]),
    };
}
