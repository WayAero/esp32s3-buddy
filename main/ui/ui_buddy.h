/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_shared.h"
#include "lvgl.h"

typedef struct {
    int32_t frame_index;
    int32_t frame_count;
    uint32_t source_width;
    uint32_t source_height;
    int64_t sample_time_us;
    bool running;
    bool loaded;
    char source[224];
} buddy_app_ui_buddy_gif_progress_t;

bool buddy_app_ui_buddy_create(void *context);
lv_obj_t *buddy_app_ui_buddy_root(void);
void **buddy_app_ui_buddy_root_slot(void);
void buddy_app_ui_buddy_layout(bool landscape, int32_t content_height,
                               int32_t inset, int32_t top, int32_t card_width);
void buddy_app_ui_buddy_apply_locale_fonts(void);
void buddy_app_ui_buddy_refresh(const example_buddy_state_cache_t *state,
                                const char *status_text, lv_color_t status_color,
                                const char *tokens_text, const char *context_text,
                                const char *context_compact_text);
void buddy_app_ui_buddy_update_gif(bool have_active,
                                   const example_charpack_info_t *active_pack,
                                   const example_buddy_state_cache_t *state_cache,
                                   const esp_desktop_buddy_transport_ble_state_t *transport);
void buddy_app_ui_buddy_set_gif_playing(bool playing);
bool buddy_app_ui_buddy_gif_loaded(void);
void buddy_app_ui_buddy_reset_gif(void);
void buddy_app_ui_buddy_sample_gif_progress(buddy_app_ui_buddy_gif_progress_t *progress);
