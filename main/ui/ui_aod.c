/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_theme.h"

#include "ui_aod.h"

#include "ui_locale.h"


#if CONFIG_LV_FONT_MONTSERRAT_14
#define BUDDY_APP_FONT_BODY (&lv_font_montserrat_14)
#else
#define BUDDY_APP_FONT_BODY LV_FONT_DEFAULT
#endif
#if CONFIG_LV_FONT_MONTSERRAT_24
#define BUDDY_APP_FONT_PASSKEY (&lv_font_montserrat_24)
#else
#define BUDDY_APP_FONT_PASSKEY BUDDY_APP_FONT_BODY
#endif
#if CONFIG_LV_FONT_MONTSERRAT_12
#define BUDDY_APP_FONT_META (&lv_font_montserrat_12)
#else
#define BUDDY_APP_FONT_META LV_FONT_DEFAULT
#endif

static struct {
    lv_obj_t *page;
    lv_obj_t *time_label;
    lv_obj_t *second_label;
    lv_obj_t *date_label;
    lv_obj_t *status_label;
} s_aod;

static void buddy_app_ui_aod_style_label(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

bool buddy_app_ui_aod_create(lv_obj_t *parent,
                             const buddy_app_ui_aod_callbacks_t *callbacks)
{
    if (s_aod.page != NULL) {
        return true;
    }
    if (parent == NULL || callbacks == NULL || callbacks->action_event == NULL) {
        return false;
    }

    s_aod.page = lv_obj_create(parent);
    if (s_aod.page == NULL) {
        return false;
    }
    lv_obj_remove_style_all(s_aod.page);
    lv_obj_set_size(s_aod.page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_aod.page, 0, 0);
    lv_obj_set_style_bg_color(s_aod.page, lv_color_hex(0x050506), 0);
    lv_obj_set_style_bg_opa(s_aod.page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_aod.page, LV_OBJ_FLAG_SCROLLABLE);

    s_aod.time_label = lv_label_create(s_aod.page);
    lv_obj_set_pos(s_aod.time_label, 18, 42);
    lv_obj_set_width(s_aod.time_label, 92);
    buddy_app_ui_aod_style_label(s_aod.time_label, BUDDY_APP_FONT_PASSKEY, lv_color_hex(0xE9ECEF));
    lv_obj_set_style_text_align(s_aod.time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_aod.time_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_aod.time_label, "--:--");

    s_aod.second_label = lv_label_create(s_aod.page);
    lv_obj_set_pos(s_aod.second_label, 45, 72);
    lv_obj_set_width(s_aod.second_label, 38);
    buddy_app_ui_aod_style_label(s_aod.second_label, BUDDY_APP_FONT_BODY, lv_color_hex(0xC9CCD0));
    lv_obj_set_style_text_align(s_aod.second_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_aod.second_label, "--");

    s_aod.date_label = lv_label_create(s_aod.page);
    lv_obj_set_pos(s_aod.date_label, 14, 97);
    lv_obj_set_width(s_aod.date_label, 100);
    buddy_app_ui_aod_style_label(s_aod.date_label, BUDDY_APP_FONT_META, lv_color_hex(0xC9CCD0));
    lv_obj_set_style_text_align(s_aod.date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_aod.date_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_aod.date_label, ui_text(UI_TEXT_CLOCK_NOT_SYNCED));

    s_aod.status_label = lv_label_create(s_aod.page);
    lv_obj_set_pos(s_aod.status_label, 12, 116);
    lv_obj_set_width(s_aod.status_label, 104);
    buddy_app_ui_aod_style_label(s_aod.status_label, BUDDY_APP_FONT_META, lv_color_hex(0x8F949A));
    lv_obj_set_style_text_align(s_aod.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_aod.status_label, LV_LABEL_LONG_DOT);

    lv_obj_add_flag(s_aod.page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_aod.page, callbacks->action_event, LV_EVENT_PRESSED,
                        (void *)(uintptr_t)callbacks->home_action);
    return true;
}

lv_obj_t *buddy_app_ui_aod_root(void)
{
    return s_aod.page;
}

void buddy_app_ui_aod_layout(int32_t screen_width, int32_t screen_height, bool landscape)
{
    if (s_aod.page == NULL) {
        return;
    }
    lv_obj_set_size(s_aod.page, screen_width, screen_height);
    lv_obj_set_pos(s_aod.page, 0, 0);
    lv_obj_set_width(s_aod.time_label, landscape ? 150 : 132);
    lv_obj_align(s_aod.time_label, LV_ALIGN_CENTER, landscape ? -18 : 0, landscape ? -34 : -48);
    lv_obj_align(s_aod.second_label, LV_ALIGN_CENTER, landscape ? 60 : 58, landscape ? -26 : -40);
    lv_obj_set_width(s_aod.date_label, landscape ? 220 : 200);
    lv_obj_align(s_aod.date_label, LV_ALIGN_CENTER, 0, landscape ? 8 : 2);
    lv_obj_set_width(s_aod.status_label, landscape ? 260 : 216);
    lv_obj_align(s_aod.status_label, LV_ALIGN_CENTER, 0, landscape ? 34 : 28);
}

void buddy_app_ui_aod_apply_locale_fonts(void)
{
    if (s_aod.page == NULL) {
        return;
    }
    lv_obj_set_style_text_font(s_aod.date_label, ui_locale_font(BUDDY_APP_FONT_META), 0);
    lv_obj_set_style_text_font(s_aod.status_label, ui_locale_font(BUDDY_APP_FONT_META), 0);
}

void buddy_app_ui_aod_refresh(const char *time_text,
                              const char *second_text,
                              const char *date_text,
                              const char *status_text)
{
    if (s_aod.page == NULL) {
        return;
    }
    lv_label_set_text(s_aod.time_label, time_text != NULL ? time_text : "");
    lv_label_set_text(s_aod.second_label, second_text != NULL ? second_text : "");
    lv_label_set_text(s_aod.date_label, date_text != NULL ? date_text : "");
    lv_label_set_text(s_aod.status_label, status_text != NULL ? status_text : "");
}
