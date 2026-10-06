/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "wear_levelling.h"

#include "example_charpack.h"

#define EXAMPLE_CHARPACK_PATH_MAX 192

typedef struct {
    bool active;
    char pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1];
    char source_name[EXAMPLE_CHARPACK_PATH_MAX];
    char current_path[EXAMPLE_CHARPACK_PATH_MAX];
    uint32_t total_bytes;
    uint32_t bytes_written;
    uint32_t file_size;
    FILE *file;
} example_charpack_transfer_t;

struct example_charpack {
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t files_mutex; /* 文件事务先取此锁，再取状态锁；回调在解锁后发出。 */
    example_charpack_event_cb_t on_event;
    void *event_ctx;
    bool mounted;
    wl_handle_t wl_handle;
    char mount_point[EXAMPLE_CHARPACK_PATH_MAX];
    char packs_root[EXAMPLE_CHARPACK_PATH_MAX];
    char staging_root[EXAMPLE_CHARPACK_PATH_MAX];
    char active_path[EXAMPLE_CHARPACK_PATH_MAX];
    const char *partition_label;
    bool format_if_mount_failed;
    esp_desktop_buddy_folder_push_sink_t sink;
    example_charpack_transfer_t transfer;
    example_charpack_diagnostics_t diagnostics;
    example_charpack_info_t active_info;
    bool active_present;
    bool selection_active; /* 覆盖活动选择及其解锁后的通知，防止通知顺序反转。 */
};

void example_charpack_emit_event(example_charpack_t *charpack,
                                   example_charpack_event_type_t type,
                                   const example_charpack_info_t *info,
                                   const char *path,
                                   uint32_t size,
                                   uint32_t bytes_written,
                                   uint32_t total_bytes);

bool example_charpack_is_safe_pack_id(const char *pack_id);
bool example_charpack_normalize_pack_id(const char *source_name,
                                          char *pack_id,
                                          size_t pack_id_size);
esp_err_t example_charpack_read_manifest_info(const char *manifest_path,
                                                const char *expected_pack_id,
                                                const char *expected_source_name,
                                                example_charpack_info_t *out_info,
                                                const char **out_error_token);
