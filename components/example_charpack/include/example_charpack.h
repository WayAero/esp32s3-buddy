/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "esp_desktop_buddy/folder_push.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXAMPLE_CHARPACK_PACK_ID_MAX CONFIG_EXAMPLE_CHARPACK_PACK_ID_MAX

typedef struct example_charpack example_charpack_t;

typedef enum {
    EXAMPLE_CHARPACK_MODE_GIF = 0,
    EXAMPLE_CHARPACK_MODE_TEXT,
} example_charpack_mode_t;

typedef struct {
    char pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1];
    example_charpack_mode_t mode;
} example_charpack_info_t;

typedef enum {
    EXAMPLE_CHARPACK_EVENT_TRANSFER_STARTED = 0,
    EXAMPLE_CHARPACK_EVENT_FILE_STARTED,
    EXAMPLE_CHARPACK_EVENT_TRANSFER_PROGRESS,
    EXAMPLE_CHARPACK_EVENT_FILE_FINISHED,
    EXAMPLE_CHARPACK_EVENT_TRANSFER_ABORTED,
    EXAMPLE_CHARPACK_EVENT_INSTALL_SUCCEEDED,
    EXAMPLE_CHARPACK_EVENT_INSTALL_FAILED,
    EXAMPLE_CHARPACK_EVENT_ACTIVE_CHANGED,
    EXAMPLE_CHARPACK_EVENT_ACTIVE_CLEARED,
} example_charpack_event_type_t;

typedef struct {
    example_charpack_event_type_t type;
    example_charpack_info_t info;
    const char *path;
    uint32_t size;
    uint32_t bytes_written;
    uint32_t total_bytes;
} example_charpack_event_t;

/** Cumulative synchronous staging-write diagnostics. */
typedef struct {
    uint32_t chunk_write_count;
    uint64_t fwrite_total_us;
    uint32_t fwrite_max_us;
    uint32_t file_finalize_count;
    uint64_t file_finalize_total_us;
    uint32_t file_finalize_max_us;
} example_charpack_diagnostics_t;

typedef void (*example_charpack_event_cb_t)(void *ctx,
                                              const example_charpack_event_t *event);

typedef struct {
    const char *mount_point;
    const char *packs_root;
    const char *staging_root;
    bool format_if_mount_failed;
    example_charpack_event_cb_t on_event;
    void *event_ctx;
} example_charpack_config_t;

esp_err_t example_charpack_new(const example_charpack_config_t *config,
                                 example_charpack_t **out_charpack);
void example_charpack_delete(example_charpack_t *charpack);
const esp_desktop_buddy_folder_push_sink_t *example_charpack_get_sink(example_charpack_t *charpack);

esp_err_t example_charpack_list(example_charpack_t *charpack,
                                  example_charpack_info_t *items,
                                  size_t capacity,
                                  size_t *out_count);
esp_err_t example_charpack_get_active(example_charpack_t *charpack,
                                        example_charpack_info_t *out_info);
esp_err_t example_charpack_get_diagnostics(example_charpack_t *charpack,
                                            example_charpack_diagnostics_t *out_diagnostics);
esp_err_t example_charpack_set_active(example_charpack_t *charpack,
                                        const char *pack_id);

#ifdef __cplusplus
}
#endif
