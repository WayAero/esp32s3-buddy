/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

#include "ui_diagnostics.h"
#include "ui_locale.h"


#if CONFIG_LV_FONT_MONTSERRAT_18
#define BUDDY_APP_UI_FONT_TITLE (&lv_font_montserrat_18)
#else
#define BUDDY_APP_UI_FONT_TITLE LV_FONT_DEFAULT
#endif

#if CONFIG_LV_FONT_MONTSERRAT_12
#define BUDDY_APP_UI_FONT_META (&lv_font_montserrat_12)
#else
#define BUDDY_APP_UI_FONT_META LV_FONT_DEFAULT
#endif

typedef struct {
    lv_obj_t *page;
    lv_obj_t *title_label;
    lv_obj_t *back_button;
    lv_obj_t *rows[BUDDY_APP_UI_DIAGNOSTICS_ROWS];
    lv_obj_t *name_labels[BUDDY_APP_UI_DIAGNOSTICS_ROWS];
    lv_obj_t *value_labels[BUDDY_APP_UI_DIAGNOSTICS_ROWS];
    lv_obj_t *service_label;
} buddy_app_ui_diagnostics_t;

static buddy_app_ui_diagnostics_t s_diagnostics;
static lv_event_cb_t s_back_event, s_track_tap_event;

void buddy_app_ui_diagnostics_set_navigation(lv_event_cb_t back_event, lv_event_cb_t track_tap_event)
{
    s_back_event = back_event;
    s_track_tap_event = track_tap_event;
}

static void buddy_app_ui_diagnostics_style_label(lv_obj_t *label,
                                                  const lv_font_t *font,
                                                  lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static lv_obj_t *buddy_app_ui_diagnostics_create_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, 0, 0);
    lv_obj_set_size(card, 44, 44);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(BUDDY_APP_COLOR_INFO_BG), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, BUDDY_APP_UI_CARD_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(BUDDY_APP_UI_COLOR_PANEL_ALT), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(BUDDY_APP_UI_COLOR_ACCENT), 0);
    lv_obj_set_style_outline_opa(card, LV_OPA_60, 0);
    return card;
}

bool buddy_app_ui_diagnostics_create(void *context)
{
    lv_obj_t *parent = context;

    if (s_diagnostics.page != NULL) return true;
    if (parent == NULL) return false;
    s_diagnostics.page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_diagnostics.page);
    lv_obj_set_size(s_diagnostics.page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_diagnostics.page, 0, 0);
    lv_obj_set_style_bg_opa(s_diagnostics.page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_diagnostics.page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_diagnostics.title_label = lv_label_create(s_diagnostics.page);
    buddy_app_ui_diagnostics_style_label(s_diagnostics.title_label, BUDDY_APP_UI_FONT_TITLE,
                                         lv_color_hex(BUDDY_APP_UI_COLOR_TEXT));
    lv_label_set_long_mode(s_diagnostics.title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_diagnostics.title_label, ui_text(UI_TEXT_DIAGNOSTICS));
    s_diagnostics.back_button = lv_button_create(s_diagnostics.page);
    lv_obj_set_size(s_diagnostics.back_button, 44, 44);
    lv_obj_set_style_radius(s_diagnostics.back_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(s_diagnostics.back_button, 0, 0);
    lv_obj_set_style_bg_color(s_diagnostics.back_button, lv_color_hex(BUDDY_APP_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(s_diagnostics.back_button, BUDDY_APP_UI_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(s_diagnostics.back_button, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), 0);
    lv_obj_t *back_icon = lv_label_create(s_diagnostics.back_button);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(BUDDY_APP_COLOR_ACCENT), 0);
    lv_obj_center(back_icon);
    if (s_track_tap_event != NULL)
        lv_obj_add_event_cb(s_diagnostics.back_button, s_track_tap_event, LV_EVENT_ALL, NULL);
    if (s_back_event != NULL)
        lv_obj_add_event_cb(s_diagnostics.back_button, s_back_event, LV_EVENT_CLICKED, NULL);
    for (size_t i = 0; i < BUDDY_APP_UI_DIAGNOSTICS_ROWS; ++i) {
        s_diagnostics.rows[i] = buddy_app_ui_diagnostics_create_card(s_diagnostics.page);
        s_diagnostics.name_labels[i] = lv_label_create(s_diagnostics.rows[i]);
        buddy_app_ui_diagnostics_style_label(s_diagnostics.name_labels[i], BUDDY_APP_UI_FONT_META,
                                             lv_color_hex(BUDDY_APP_UI_COLOR_MUTED));
        lv_label_set_long_mode(s_diagnostics.name_labels[i], LV_LABEL_LONG_DOT);
        s_diagnostics.value_labels[i] = lv_label_create(s_diagnostics.rows[i]);
        buddy_app_ui_diagnostics_style_label(s_diagnostics.value_labels[i], BUDDY_APP_UI_FONT_META,
                                             lv_color_hex(BUDDY_APP_UI_COLOR_TEXT));
        lv_obj_set_style_text_align(s_diagnostics.value_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_diagnostics.value_labels[i], LV_LABEL_LONG_DOT);
    }
    s_diagnostics.service_label = lv_label_create(s_diagnostics.page);
    buddy_app_ui_diagnostics_style_label(s_diagnostics.service_label, BUDDY_APP_UI_FONT_META,
                                         lv_color_hex(BUDDY_APP_UI_COLOR_MUTED));
    lv_label_set_long_mode(s_diagnostics.service_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_diagnostics.service_label, ui_text(UI_TEXT_DIAG_SERVICES_OK));
    return s_diagnostics.title_label != NULL && s_diagnostics.rows[0] != NULL &&
           s_diagnostics.rows[1] != NULL && s_diagnostics.rows[2] != NULL &&
           s_diagnostics.rows[3] != NULL && s_diagnostics.rows[4] != NULL &&
           s_diagnostics.rows[5] != NULL && s_diagnostics.service_label != NULL;
}

lv_obj_t *buddy_app_ui_diagnostics_root(void) { return s_diagnostics.page; }

void **buddy_app_ui_diagnostics_root_slot(void) { return (void **)&s_diagnostics.page; }

void buddy_app_ui_diagnostics_layout(bool landscape,
                                     int32_t content_height,
                                     int32_t inset,
                                     int32_t gap,
                                     int32_t top,
                                     int32_t card_width)
{
    if (s_diagnostics.page == NULL) return;
    lv_obj_set_pos(s_diagnostics.title_label, inset, top + 13);
    lv_obj_set_width(s_diagnostics.title_label, card_width - 50);
    lv_obj_set_pos(s_diagnostics.back_button, inset + card_width - 44, top);
    for (size_t i = 0; i < BUDDY_APP_UI_DIAGNOSTICS_ROWS; ++i) {
        if (landscape) {
            int32_t diag_width = (card_width - gap) / 2;
            int32_t column = (int32_t)(i / 3U);
            int32_t row = (int32_t)(i % 3U);
            lv_obj_set_pos(s_diagnostics.rows[i], inset + column * (diag_width + gap),
                           top + 50 + row * 38);
            lv_obj_set_size(s_diagnostics.rows[i], diag_width, 34);
        } else {
            lv_obj_set_pos(s_diagnostics.rows[i], inset, top + 50 + (int32_t)i * 28);
            lv_obj_set_size(s_diagnostics.rows[i], card_width, 26);
        }
        lv_obj_set_pos(s_diagnostics.name_labels[i], 7, landscape ? 5 : 4);
        lv_obj_set_width(s_diagnostics.name_labels[i], LV_PCT(42));
        lv_obj_align(s_diagnostics.value_labels[i], LV_ALIGN_RIGHT_MID, -7, 0);
        lv_obj_set_width(s_diagnostics.value_labels[i], LV_PCT(54));
    }
    lv_obj_set_pos(s_diagnostics.service_label, inset, landscape ? top + 174 : top + 220);
    lv_obj_set_width(s_diagnostics.service_label, card_width);
    (void)content_height;
}

void buddy_app_ui_diagnostics_apply_locale_fonts(void)
{
    const lv_font_t *title = ui_locale_font(BUDDY_APP_UI_FONT_TITLE);
    const lv_font_t *meta = ui_locale_font(BUDDY_APP_UI_FONT_META);

    if (s_diagnostics.page == NULL) return;
    lv_obj_set_style_text_font(s_diagnostics.title_label, title, 0);
    for (size_t i = 0; i < BUDDY_APP_UI_DIAGNOSTICS_ROWS; ++i) {
        lv_obj_set_style_text_font(s_diagnostics.name_labels[i], meta, 0);
        lv_obj_set_style_text_font(s_diagnostics.value_labels[i], meta, 0);
    }
    lv_obj_set_style_text_font(s_diagnostics.service_label, meta, 0);
}

void buddy_app_ui_diagnostics_refresh(buddy_app_t *app, bool active)
{
    static buddy_app_diag_snapshot_t diag;
    static char values[BUDDY_APP_UI_DIAGNOSTICS_ROWS][96];
    static char service_text[96];
    static const char *const names_en[BUDDY_APP_UI_DIAGNOSTICS_ROWS] = {
        "Firmware", "Git", "ESP-IDF", "Build", "Heap", "Stacks"
    };
    static const char *const names_zh[BUDDY_APP_UI_DIAGNOSTICS_ROWS] = {
        "固件", "Git", "ESP-IDF", "构建", "内存", "栈余量"
    };

    if (app == NULL || s_diagnostics.page == NULL) {
        return;
    }
    lv_label_set_text(s_diagnostics.title_label, ui_text(UI_TEXT_DIAGNOSTICS));
    if (!active) {
        return;
    }
    buddy_app_diag_snapshot_get(app, &diag);
    strlcpy(values[0], diag.version[0] ? diag.version : "--", sizeof(values[0]));
    strlcpy(values[1], diag.git_commit[0] ? diag.git_commit : "--", sizeof(values[1]));
    strlcpy(values[2], diag.idf_version[0] ? diag.idf_version : "--", sizeof(values[2]));
    strlcpy(values[3], diag.build[0] ? diag.build : "--", sizeof(values[3]));
    snprintf(values[4],
             sizeof(values[4]),
             "I%lu D%lu P%lu KB",
             (unsigned long)(diag.internal_free / 1024U),
             (unsigned long)(diag.dma_free / 1024U),
             (unsigned long)(diag.psram_free / 1024U));
    snprintf(values[5], sizeof(values[5]), "%lu/%lu/%lu B",
             (unsigned long)diag.task_stack_bytes[BUDDY_APP_DIAG_TASK_UI],
             (unsigned long)diag.task_stack_bytes[BUDDY_APP_DIAG_TASK_SETTINGS],
             (unsigned long)diag.task_stack_bytes[BUDDY_APP_DIAG_TASK_STATUS_LED]);
    strlcpy(service_text, diag.service_error[0] ? ui_text(UI_TEXT_DIAG_SERVICES_ERROR) :
                                              ui_text(UI_TEXT_DIAG_SERVICES_OK), sizeof(service_text));
    for (size_t i = 0; i < BUDDY_APP_UI_DIAGNOSTICS_ROWS; ++i) {
        lv_label_set_text(s_diagnostics.name_labels[i],
                          ui_locale_get() == UI_LOCALE_ZH_CN ? names_zh[i] : names_en[i]);
        lv_label_set_text(s_diagnostics.value_labels[i], values[i]);
    }
    lv_label_set_text(s_diagnostics.service_label, service_text);
}
