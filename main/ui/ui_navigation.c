/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "ui_navigation.h"

static buddy_app_ui_page_t s_current_page = BUDDY_APP_PAGE_HOME;
static buddy_app_ui_page_t s_rendered_page = BUDDY_APP_PAGE_COUNT;
static buddy_app_ui_page_t s_page_before_settings = BUDDY_APP_PAGE_HOME;

void buddy_app_ui_navigation_init(void)
{
    s_current_page = BUDDY_APP_PAGE_HOME;
    s_rendered_page = BUDDY_APP_PAGE_COUNT;
    s_page_before_settings = BUDDY_APP_PAGE_HOME;
}

buddy_app_ui_page_t buddy_app_ui_navigation_current(void) { return s_current_page; }
buddy_app_ui_page_t buddy_app_ui_navigation_rendered(void) { return s_rendered_page; }
buddy_app_ui_page_t buddy_app_ui_navigation_page_before_settings(void) { return s_page_before_settings; }
void buddy_app_ui_navigation_go(buddy_app_ui_page_t page) { s_current_page = page; }
void buddy_app_ui_navigation_set_rendered(buddy_app_ui_page_t page) { s_rendered_page = page; }
void buddy_app_ui_navigation_set_page_before_settings(buddy_app_ui_page_t page) { s_page_before_settings = page; }

void buddy_app_ui_navigation_set_visible(lv_obj_t *page, bool visible)
{
    bool hidden;
    if (page == NULL) return;
    hidden = lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN);
    if (visible == !hidden) return;
    if (visible) lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
}
