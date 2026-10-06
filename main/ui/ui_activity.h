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

enum { BUDDY_APP_UI_ACTIVITY_VISIBLE = 3 };

const char *buddy_app_ui_activity_event_text(const buddy_app_activity_entry_t *entry);
bool buddy_app_ui_activity_create(lv_obj_t *parent);
lv_obj_t *buddy_app_ui_activity_root(void);
void buddy_app_ui_activity_layout(bool landscape,
                                  int32_t content_width,
                                  int32_t content_height,
                                  int32_t inset,
                                  int32_t top,
                                  int32_t card_width);
void buddy_app_ui_activity_apply_locale_fonts(void);
void buddy_app_ui_activity_refresh(buddy_app_t *app);
