/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "buddy_internal.h"

static esp_err_t buddy_codec_queue_root(esp_desktop_buddy_t *buddy,
                                        cJSON *root,
                                        bool is_folder_chunk_ack)
{
    char *json;
    size_t len;
    uint8_t *frame;
    esp_err_t err;

    json = cJSON_PrintUnformatted(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    len = strlen(json);
    if (len > ESP_DESKTOP_BUDDY_LINE_MAX) {
        cJSON_free(json);
        return ESP_ERR_INVALID_SIZE;
    }

    buddy_log_protocol_line("buddy_codec", "tx", json, len);

    frame = malloc(len + 1);
    if (frame == NULL) {
        cJSON_free(json);
        return ESP_ERR_NO_MEM;
    }

    memcpy(frame, json, len);
    frame[len] = '\n';
    err = esp_desktop_buddy_txq_enqueue_frame(buddy, frame, len + 1, is_folder_chunk_ack);
    free(frame);
    cJSON_free(json);
    return err;
}

esp_err_t buddy_codec_queue_ack(esp_desktop_buddy_t *buddy,
                                const char *ack,
                                bool ok,
                                uint32_t n,
                                const char *error)
{
    cJSON *root = cJSON_CreateObject();
    esp_err_t err;
    const bool is_folder_chunk = ack != NULL && strcmp(ack, "chunk") == 0;
    int64_t encode_started_us = 0;

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "ack", ack);
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddNumberToObject(root, "n", (double)n);
    if (error != NULL) {
        cJSON_AddStringToObject(root, "error", error);
    }

    if (is_folder_chunk) {
        encode_started_us = esp_timer_get_time();
    }
    err = buddy_codec_queue_root(buddy, root, is_folder_chunk);
    if (is_folder_chunk) {
        const uint32_t encode_elapsed_us = (uint32_t)(esp_timer_get_time() - encode_started_us);

        xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
        buddy->rx_diagnostics.folder_chunk_ack_encode_count++;
        buddy->rx_diagnostics.folder_chunk_ack_encode_total_us += encode_elapsed_us;
        if (encode_elapsed_us > buddy->rx_diagnostics.folder_chunk_ack_encode_max_us) {
            buddy->rx_diagnostics.folder_chunk_ack_encode_max_us = encode_elapsed_us;
        }
        xSemaphoreGive(buddy->state_mutex);
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t buddy_codec_queue_status_result(esp_desktop_buddy_t *buddy, esp_desktop_buddy_status_reply_t result)
{
    cJSON *root = NULL;
    cJSON *data = NULL;
    esp_err_t err;
    const char *error = esp_desktop_buddy_status_error_token(result.result.err);

    if (result.result.err != ESP_OK) {
        ESP_LOGW("buddy_codec",
                 "status handler failed err=%s detail=%s token=%s",
                 esp_err_to_name(result.result.err),
                 result.result.detail != NULL ? result.result.detail : "<none>",
                 error != NULL ? error : "<none>");
        return buddy_codec_queue_ack(buddy, "status", false, 0, error);
    }

    if (result.data.bytes == NULL || result.data.len == 0) {
        return buddy_codec_queue_ack(buddy, "status", false, 0, BUDDY_TOKEN_INVALID_STATUS_DATA);
    }

    data = cJSON_ParseWithLength((const char *)result.data.bytes, result.data.len);
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(data);
        return buddy_codec_queue_ack(buddy, "status", false, 0, BUDDY_TOKEN_INVALID_STATUS_DATA);
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(data);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "ack", "status");
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "n", 0);
    cJSON_AddItemToObject(root, "data", data);
    data = NULL;

    err = buddy_codec_queue_root(buddy, root, false);
    cJSON_Delete(root);

    if (err == ESP_ERR_INVALID_SIZE) {
        return buddy_codec_queue_ack(buddy, "status", false, 0, BUDDY_TOKEN_STATUS_TOO_LARGE);
    }

    return err;
}

esp_err_t buddy_codec_queue_permission_reply(esp_desktop_buddy_t *buddy,
                                             const char *prompt_id,
                                             esp_desktop_buddy_permission_decision_t decision)
{
    cJSON *root = cJSON_CreateObject();
    esp_err_t err;

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "cmd", "permission");
    cJSON_AddStringToObject(root, "id", prompt_id);
    cJSON_AddStringToObject(root,
                            "decision",
                            decision == ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY ? "deny" : "once");

    err = buddy_codec_queue_root(buddy, root, false);
    cJSON_Delete(root);
    return err;
}
