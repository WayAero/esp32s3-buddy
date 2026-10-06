/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "example_app_helpers.h"
#include "lvgl.h"

typedef struct {
    lv_event_cb_t app_card_event;
    lv_event_cb_t track_tap_event;
    lv_event_cb_t action_event;
    uintptr_t activity_action;
} buddy_app_ui_home_callbacks_t;

bool buddy_app_ui_home_create(lv_obj_t *parent,
                              const buddy_app_ui_home_callbacks_t *callbacks);

lv_obj_t *buddy_app_ui_home_root(void);

void buddy_app_ui_home_layout(bool landscape,
                              int32_t content_width,
                              int32_t content_height,
                              int32_t status_height);

void buddy_app_ui_home_apply_locale_fonts(void);

void buddy_app_ui_home_refresh(const example_buddy_state_cache_t *state,
                               bool has_snapshot,
                               const char *title_text,
                               const char *date_text,
                               const char *transport_text,
                               const char *buddy_status_text,
                               lv_color_t buddy_status_color,
                               const char *tokens_text,
                               const char *context_text,
                               const char *activity_text);
