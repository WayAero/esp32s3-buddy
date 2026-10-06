/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "ui_internal.h"
#include "lvgl.h"

void buddy_app_ui_navigation_set_visible(lv_obj_t *page, bool visible);
void buddy_app_ui_navigation_init(void);
buddy_app_ui_page_t buddy_app_ui_navigation_current(void);
buddy_app_ui_page_t buddy_app_ui_navigation_rendered(void);
buddy_app_ui_page_t buddy_app_ui_navigation_page_before_settings(void);
void buddy_app_ui_navigation_go(buddy_app_ui_page_t page);
void buddy_app_ui_navigation_set_rendered(buddy_app_ui_page_t page);
void buddy_app_ui_navigation_set_page_before_settings(buddy_app_ui_page_t page);
