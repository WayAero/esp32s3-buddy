/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "app_shared.h"
#include "lvgl.h"

enum { BUDDY_APP_UI_DIAGNOSTICS_ROWS = 6 };

/* 首次惰性创建页面前由 UI 协调模块绑定返回和触摸跟踪。 */
void buddy_app_ui_diagnostics_set_navigation(lv_event_cb_t back_event, lv_event_cb_t track_tap_event);

bool buddy_app_ui_diagnostics_create(void *context);
lv_obj_t *buddy_app_ui_diagnostics_root(void);
void **buddy_app_ui_diagnostics_root_slot(void);
void buddy_app_ui_diagnostics_layout(bool landscape,
                                     int32_t content_height,
                                     int32_t inset,
                                     int32_t gap,
                                     int32_t top,
                                     int32_t card_width);
void buddy_app_ui_diagnostics_apply_locale_fonts(void);

void buddy_app_ui_diagnostics_refresh(buddy_app_t *app, bool active);
