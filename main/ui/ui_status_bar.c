/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_status_bar.h"

#if CONFIG_LV_FONT_MONTSERRAT_12
#define BUDDY_APP_STATUS_BAR_FONT_META (&lv_font_montserrat_12)
#else
#define BUDDY_APP_STATUS_BAR_FONT_META LV_FONT_DEFAULT
#endif

#include "ui_theme.h"
#define BUDDY_APP_STATUS_BAR_COLOR_BG BUDDY_APP_COLOR_BG
#define BUDDY_APP_STATUS_BAR_COLOR_MUTED BUDDY_APP_COLOR_MUTED

static lv_obj_t *s_root;
static lv_obj_t *s_ble_label;
static lv_obj_t *s_time_label;

static void buddy_app_ui_status_bar_style_label(lv_obj_t *label, const lv_font_t *font)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(BUDDY_APP_STATUS_BAR_COLOR_MUTED), 0);
}

void buddy_app_ui_status_bar_create(lv_obj_t *root)
{
    if (root == NULL) {
        return;
    }

    s_root = lv_obj_create(root);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), 28);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(BUDDY_APP_STATUS_BAR_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_ble_label = lv_label_create(s_root);
    buddy_app_ui_status_bar_style_label(s_ble_label, LV_FONT_DEFAULT);
    lv_obj_set_style_text_align(s_ble_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ble_label, LV_SYMBOL_BLUETOOTH);

    s_time_label = lv_label_create(s_root);
    buddy_app_ui_status_bar_style_label(s_time_label, BUDDY_APP_STATUS_BAR_FONT_META);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_time_label, "--:--");


}

void buddy_app_ui_status_bar_layout(int32_t content_width, int32_t status_height)
{
    if (s_root == NULL) {
        return;
    }

    lv_obj_set_size(s_root, content_width, status_height);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_pos(s_ble_label, 8, 6);
    lv_obj_set_size(s_ble_label, 20, 16);
    /* 时间标签右对齐。 */
    lv_obj_set_size(s_time_label, 42, 16);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_RIGHT, -8, 6);
}

void buddy_app_ui_status_bar_set_visible(bool visible)
{
    if (s_root == NULL) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void buddy_app_ui_status_bar_refresh(bool ble_connected,
                                     const char *time_text,
                                     lv_color_t connected_color,
                                     lv_color_t disconnected_color)
{
    if (s_root == NULL) {
        return;
    }
    lv_obj_set_style_text_color(s_ble_label,
                                ble_connected ? connected_color : disconnected_color,
                                0);
    lv_label_set_text(s_time_label, time_text != NULL ? time_text : "");
}
