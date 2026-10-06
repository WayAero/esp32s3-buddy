/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "example_status.h"

#include "esp_system.h"
#include "esp_timer.h"

static uint32_t example_uptime_seconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

esp_err_t example_status_begin(example_status_doc_t *doc,
                                 const char *display_name,
                                 const char *owner_name,
                                 bool encrypted,
                                 uint32_t approvals,
                                 uint32_t denials)
{
    if (doc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(doc, 0, sizeof(*doc));
    doc->root = cJSON_CreateObject();
    if (doc->root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    doc->sys = cJSON_AddObjectToObject(doc->root, "sys");
    doc->stats = cJSON_AddObjectToObject(doc->root, "stats");
    if (doc->sys == NULL || doc->stats == NULL) {
        cJSON_Delete(doc->root);
        memset(doc, 0, sizeof(*doc));
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(doc->root, "name", display_name != NULL ? display_name : "");
    cJSON_AddStringToObject(doc->root, "owner", owner_name != NULL ? owner_name : "");
    cJSON_AddBoolToObject(doc->root, "sec", encrypted);
    cJSON_AddNumberToObject(doc->sys, "up", (double)example_uptime_seconds());
    cJSON_AddNumberToObject(doc->sys, "heap", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(doc->stats, "appr", (double)approvals);
    cJSON_AddNumberToObject(doc->stats, "deny", (double)denials);
    return ESP_OK;
}

esp_desktop_buddy_status_reply_t example_status_from_json(cJSON *root, char *buf, size_t buf_size)
{
    esp_desktop_buddy_json_object_view_t view = {0};

    if (root == NULL || buf == NULL || buf_size == 0) {
        cJSON_Delete(root);
        return esp_desktop_buddy_status_err(ESP_FAIL, "invalid_status_buffer");
    }

    memset(buf, 0, buf_size);
    if (!cJSON_PrintPreallocated(root, buf, buf_size, false)) {
        cJSON_Delete(root);
        return esp_desktop_buddy_status_err(ESP_ERR_INVALID_SIZE, "status_too_large");
    }
    cJSON_Delete(root);

    view.bytes = (const uint8_t *)buf;
    view.len = strlen(buf);
    return esp_desktop_buddy_status_ok(view);
}
