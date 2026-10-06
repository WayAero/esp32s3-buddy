/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    cJSON *root;
    cJSON *sys;
    cJSON *stats;
} example_status_doc_t;

esp_err_t example_status_begin(example_status_doc_t *doc,
                                 const char *display_name,
                                 const char *owner_name,
                                 bool encrypted,
                                 uint32_t approvals,
                                 uint32_t denials);
esp_desktop_buddy_status_reply_t example_status_from_json(cJSON *root, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
