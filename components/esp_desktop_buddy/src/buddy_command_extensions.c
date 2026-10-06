/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "buddy_command_extensions_internal.h"

static esp_err_t buddy_command_request_lookup_item(const esp_desktop_buddy_command_view_t *request,
                                                   const char *key,
                                                   const cJSON **out_item)
{
    if (request == NULL || key == NULL || out_item == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (request->root == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    *out_item = cJSON_GetObjectItemCaseSensitive((cJSON *)request->root, key);
    return *out_item != NULL ? ESP_OK : ESP_ERR_NOT_FOUND;
}

const char *esp_desktop_buddy_command_view_name(const esp_desktop_buddy_command_view_t *request)
{
    if (request == NULL) {
        return NULL;
    }

    return request->command;
}

esp_err_t esp_desktop_buddy_command_view_get_string(const esp_desktop_buddy_command_view_t *request,
                                           const char *key,
                                           const char **out_value)
{
    const cJSON *item = NULL;
    esp_err_t err = buddy_command_request_lookup_item(request, key, &item);

    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    *out_value = item->valuestring;
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_command_view_get_u32(const esp_desktop_buddy_command_view_t *request,
                                        const char *key,
                                        uint32_t *out_value)
{
    const cJSON *item = NULL;
    double value;
    esp_err_t err;

    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = buddy_command_request_lookup_item(request, key, &item);
    if (err != ESP_OK) {
        return err;
    }
    if (!cJSON_IsNumber(item)) {
        return ESP_ERR_NOT_FOUND;
    }

    value = item->valuedouble;
    if (value <= 0) {
        *out_value = 0;
        return ESP_OK;
    }
    if (value >= (double)UINT32_MAX) {
        *out_value = UINT32_MAX;
        return ESP_OK;
    }

    *out_value = (uint32_t)value;
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_command_view_get_u32_strict(const esp_desktop_buddy_command_view_t *request,
                                                        const char *key,
                                                        uint32_t *out_value)
{
    const cJSON *item = NULL;
    double value;
    esp_err_t err;

    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = buddy_command_request_lookup_item(request, key, &item);
    if (err != ESP_OK || !cJSON_IsNumber(item)) {
        return ESP_ERR_NOT_FOUND;
    }

    value = item->valuedouble;
    if (value < 0 || value > (double)UINT32_MAX || value != (double)(uint32_t)value) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_value = (uint32_t)value;
    return ESP_OK;
}
