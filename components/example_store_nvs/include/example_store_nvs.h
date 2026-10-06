/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t example_store_nvs_write_string(const char *ns,
                                           const char *key,
                                           const char *value);
esp_err_t example_store_nvs_read_string(const char *ns,
                                          const char *key,
                                          char *dst,
                                          size_t dst_size);
esp_err_t example_store_nvs_erase_key(const char *ns, const char *key);

#ifdef __cplusplus
}
#endif
