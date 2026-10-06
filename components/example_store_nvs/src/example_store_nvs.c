/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nvs.h"

#include "example_store_nvs.h"

esp_err_t example_store_nvs_write_string(const char *ns,
                                           const char *key,
                                           const char *value)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (ns == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t example_store_nvs_read_string(const char *ns,
                                          const char *key,
                                          char *dst,
                                          size_t dst_size)
{
    nvs_handle_t handle;
    size_t required;
    esp_err_t err;

    if (ns == NULL || key == NULL || dst == NULL || dst_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        dst[0] = '\0';
        return err;
    }

    required = dst_size;
    err = nvs_get_str(handle, key, dst, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        dst[0] = '\0';
    }
    return err;
}

esp_err_t example_store_nvs_erase_key(const char *ns, const char *key)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (ns == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
