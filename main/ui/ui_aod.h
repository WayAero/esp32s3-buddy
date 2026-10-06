/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef struct {
    lv_event_cb_t action_event;
    uintptr_t home_action;
} buddy_app_ui_aod_callbacks_t;

bool buddy_app_ui_aod_create(lv_obj_t *parent,
                             const buddy_app_ui_aod_callbacks_t *callbacks);
lv_obj_t *buddy_app_ui_aod_root(void);
void buddy_app_ui_aod_layout(int32_t screen_width, int32_t screen_height, bool landscape);
void buddy_app_ui_aod_apply_locale_fonts(void);
void buddy_app_ui_aod_refresh(const char *time_text,
                              const char *second_text,
                              const char *date_text,
                              const char *status_text);
