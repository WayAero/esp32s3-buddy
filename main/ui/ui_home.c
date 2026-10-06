/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_theme.h"

#include "ui_home.h"

#include <stddef.h>
#include <string.h>
#include "ui_app_registry.h"
#include "ui_format.h"
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

#if CONFIG_LV_FONT_MONTSERRAT_18
#define BUDDY_APP_FONT_TITLE (&lv_font_montserrat_18)
#else
#define BUDDY_APP_FONT_TITLE BUDDY_APP_FONT_BODY
#endif

#if CONFIG_LV_FONT_MONTSERRAT_12
#define BUDDY_APP_FONT_META (&lv_font_montserrat_12)
#else
#define BUDDY_APP_FONT_META LV_FONT_DEFAULT
#endif

typedef struct {
    lv_obj_t *page;
    lv_obj_t *title_card;
    lv_obj_t *status_card;
    lv_obj_t *activity_card;
    lv_obj_t *title_label;
    lv_obj_t *date_label;
    lv_obj_t *transport_label;
    lv_obj_t *buddy_status_label;
    lv_obj_t *buddy_metric_labels[3];
    lv_obj_t *buddy_metric_titles[3];
    lv_obj_t *buddy_usage_labels[2];
    lv_obj_t *buddy_usage_titles[2];
    lv_obj_t *buddy_empty_label;
    lv_obj_t *activity_label;
} buddy_app_ui_home_t;

/* 时钟每秒采样不会让未改变的 Buddy／Activity 文本重复失效。 */
static void buddy_app_ui_home_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static buddy_app_ui_home_t s_home;

static void buddy_app_ui_home_set_usage_text(lv_obj_t *label, const char *text)
{
    if (text == NULL) {
        buddy_app_ui_home_set_text(label, "--");
        return;
    }
    /* 输入来自 compact_u64。三位整数时省去小数，让最宽的 888.8M
     * 也能保留单位；Buddy 页继续显示完整的紧凑数值。 */
    if (strlen(text) == 6 && text[3] == '.') {
        const char compact[] = {text[0], text[1], text[2], text[5], '\0'};
        buddy_app_ui_home_set_text(label, compact);
        return;
    }
    buddy_app_ui_home_set_text(label, text);
}

static void buddy_app_ui_home_style_label(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static lv_obj_t *buddy_app_ui_home_create_card(lv_obj_t *parent,
                                                uint32_t bg_color,
                                                uint32_t border_color)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, 0, 0);
    lv_obj_set_size(card, 44, 44);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, BUDDY_APP_UI_CARD_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(border_color), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(BUDDY_APP_COLOR_ACCENT), 0);
    lv_obj_set_style_outline_opa(card, LV_OPA_60, 0);
    return card;
}

bool buddy_app_ui_home_create(lv_obj_t *parent, const buddy_app_ui_home_callbacks_t *callbacks)
{
    if (parent == NULL || callbacks == NULL || callbacks->app_card_event == NULL ||
        callbacks->track_tap_event == NULL || callbacks->action_event == NULL) {
        return false;
    }

    s_home.page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_home.page);
    lv_obj_set_size(s_home.page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_home.page, 0, 0);
    lv_obj_set_style_bg_opa(s_home.page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_home.page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_home.title_card = buddy_app_ui_home_create_card(s_home.page, BUDDY_APP_COLOR_INFO_BG,
                                                      BUDDY_APP_COLOR_PANEL_ALT);
    s_home.title_label = lv_label_create(s_home.title_card);
    lv_obj_set_pos(s_home.title_label, 6, 5);
    lv_obj_set_width(s_home.title_label, 108);
    buddy_app_ui_home_style_label(s_home.title_label, BUDDY_APP_FONT_PASSKEY,
                                  lv_color_hex(BUDDY_APP_COLOR_ACCENT));
    lv_label_set_long_mode(s_home.title_label, LV_LABEL_LONG_DOT);
    buddy_app_ui_home_set_text(s_home.title_label, "--:--");

    s_home.date_label = lv_label_create(s_home.title_card);
    lv_obj_set_pos(s_home.date_label, 6, 31);
    lv_obj_set_width(s_home.date_label, 108);
    buddy_app_ui_home_style_label(s_home.date_label, BUDDY_APP_FONT_META,
                                  lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_label_set_long_mode(s_home.date_label, LV_LABEL_LONG_DOT);
    buddy_app_ui_home_set_text(s_home.date_label, ui_text(UI_TEXT_CLOCK_NOT_SYNCED));

    s_home.status_card = buddy_app_ui_home_create_card(s_home.page, BUDDY_APP_COLOR_INFO_BG,
                                                       BUDDY_APP_COLOR_PANEL_ALT);
    s_home.transport_label = lv_label_create(s_home.status_card);
    lv_obj_set_pos(s_home.transport_label, 6, 4);
    lv_obj_set_width(s_home.transport_label, 108);
    buddy_app_ui_home_style_label(s_home.transport_label, BUDDY_APP_FONT_META,
                                  lv_color_hex(BUDDY_APP_COLOR_TEXT));
    lv_label_set_long_mode(s_home.transport_label, LV_LABEL_LONG_DOT);
    buddy_app_ui_home_set_text(s_home.transport_label, ui_text(UI_TEXT_BUDDY));
    s_home.buddy_status_label = lv_label_create(s_home.status_card);
    buddy_app_ui_home_style_label(s_home.buddy_status_label, BUDDY_APP_FONT_META,
                                  lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_obj_set_style_text_align(s_home.buddy_status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_home.buddy_status_label, LV_LABEL_LONG_DOT);
    buddy_app_ui_home_set_text(s_home.buddy_status_label, ui_text(UI_TEXT_DISCONNECTED));
    for (size_t i = 0; i < 3; ++i) {
        s_home.buddy_metric_labels[i] = lv_label_create(s_home.status_card);
        buddy_app_ui_home_style_label(s_home.buddy_metric_labels[i], BUDDY_APP_FONT_META,
                                      lv_color_hex(BUDDY_APP_COLOR_TEXT));
        /* 数值和标题各占一行，紧凑数值使用整列宽度，保留单位后缀。 */
        lv_label_set_long_mode(s_home.buddy_metric_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_home.buddy_metric_labels[i], LV_OBJ_FLAG_HIDDEN);

        s_home.buddy_metric_titles[i] = lv_label_create(s_home.status_card);
        buddy_app_ui_home_style_label(s_home.buddy_metric_titles[i], BUDDY_APP_FONT_META,
                                      lv_color_hex(BUDDY_APP_COLOR_MUTED));
        lv_label_set_long_mode(s_home.buddy_metric_titles[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_home.buddy_metric_titles[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t i = 0; i < 2; ++i) {
        s_home.buddy_usage_labels[i] = lv_label_create(s_home.status_card);
        buddy_app_ui_home_style_label(s_home.buddy_usage_labels[i], BUDDY_APP_FONT_META,
                                      lv_color_hex(BUDDY_APP_COLOR_MUTED));
        lv_label_set_long_mode(s_home.buddy_usage_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_home.buddy_usage_labels[i], LV_OBJ_FLAG_HIDDEN);

        s_home.buddy_usage_titles[i] = lv_label_create(s_home.status_card);
        buddy_app_ui_home_style_label(s_home.buddy_usage_titles[i], BUDDY_APP_FONT_META,
                                      lv_color_hex(BUDDY_APP_COLOR_MUTED));
        lv_label_set_long_mode(s_home.buddy_usage_titles[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_home.buddy_usage_titles[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_home.buddy_empty_label = lv_label_create(s_home.status_card);
    buddy_app_ui_home_style_label(s_home.buddy_empty_label, BUDDY_APP_FONT_META,
                                  lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_obj_set_pos(s_home.buddy_empty_label, 8, 31);
    lv_obj_set_size(s_home.buddy_empty_label, LV_PCT(92), 18);
    lv_label_set_long_mode(s_home.buddy_empty_label, LV_LABEL_LONG_DOT);
    buddy_app_ui_home_set_text(s_home.buddy_empty_label, ui_text(UI_TEXT_NO_SESSION));

    s_home.activity_card = buddy_app_ui_home_create_card(s_home.page, BUDDY_APP_COLOR_INFO_BG,
                                                         BUDDY_APP_COLOR_PANEL_ALT);
    s_home.activity_label = lv_label_create(s_home.activity_card);
    buddy_app_ui_home_style_label(s_home.activity_label, BUDDY_APP_FONT_META,
                                  lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_label_set_long_mode(s_home.activity_label, LV_LABEL_LONG_WRAP);
    buddy_app_ui_home_set_text(s_home.activity_label, ui_text(UI_TEXT_NO_RECENT_EVENTS));

    lv_obj_add_flag(s_home.status_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_home.status_card, callbacks->app_card_event, LV_EVENT_ALL,
                        (void *)(uintptr_t)UI_APP_ID_BUDDY);
    lv_obj_add_flag(s_home.activity_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_home.activity_card, callbacks->track_tap_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_home.activity_card, callbacks->action_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)callbacks->activity_action);
    return true;
}

lv_obj_t *buddy_app_ui_home_root(void)
{
    return s_home.page;
}

void buddy_app_ui_home_layout(bool landscape,
                              int32_t content_width,
                              int32_t content_height,
                              int32_t status_height)
{
    const int32_t inset = 10;
    const int32_t gap = 8;
    const int32_t top = status_height + 4;
    const int32_t card_width = content_width - inset * 2;
    const int32_t buddy_width = card_width;
    if (s_home.page == NULL) return;
    lv_obj_set_size(s_home.page, content_width, content_height);
    lv_obj_set_pos(s_home.page, 0, 0);
    const int32_t clock_height = landscape ? 58 : 74;
    const int32_t status_height_px = landscape ? 84 : 96;
    lv_obj_set_pos(s_home.title_card, inset, top);
    lv_obj_set_size(s_home.title_card, card_width, clock_height);
    lv_obj_set_pos(s_home.status_card, inset, top + clock_height + gap);
    lv_obj_set_size(s_home.status_card, buddy_width, status_height_px);
    int32_t activity_top = top + clock_height + status_height_px + gap * 2;
    lv_obj_set_pos(s_home.activity_card, inset, activity_top);
    lv_obj_set_size(s_home.activity_card, card_width, content_height - activity_top - 6);
    lv_obj_set_pos(s_home.title_label, 8, landscape ? 6 : 4);
    lv_obj_set_width(s_home.title_label, landscape ? 124 : card_width - 16);
    lv_obj_set_style_text_font(s_home.title_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_align(s_home.title_label, LV_TEXT_ALIGN_CENTER, 0);
    /* 横屏日期与时钟并排，避免缩小时钟卡片后压到日期的基线。 */
    lv_obj_set_pos(s_home.date_label, landscape ? 140 : 8, landscape ? 22 : 53);
    lv_obj_set_width(s_home.date_label, landscape ? card_width - 148 : card_width - 16);
    lv_obj_set_style_text_align(s_home.date_label, landscape ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_home.transport_label, 8, 5);
    lv_obj_set_size(s_home.transport_label, landscape ? 48 : 62, 18);
    lv_obj_set_pos(s_home.buddy_status_label, landscape ? 58 : 78, 5);
    lv_obj_set_size(s_home.buddy_status_label,
                    buddy_width - (landscape ? 66 : 86), 16);
    const int32_t metric_inset = 8;
    /* set_size 只更新样式；坐标要到布局执行后才更新，不能从对象读取新宽度。 */
    const int32_t metrics_width = buddy_width - metric_inset * 2;
    for (size_t i = 0; i < 3; ++i) {
        const int32_t metric_width = metrics_width / 3;
        const int32_t metric_x = metric_inset + (int32_t)i * metric_width;

        lv_obj_set_pos(s_home.buddy_metric_titles[i], metric_x, 21);
        lv_obj_set_size(s_home.buddy_metric_titles[i], metric_width - 2, 16);
        lv_obj_set_pos(s_home.buddy_metric_labels[i], metric_x, 36);
        lv_obj_set_size(s_home.buddy_metric_labels[i], metric_width - 2, 16);
    }
    /* Token 和上下文各占一整行；标题与右对齐数值有固定间隔，
     * 不再依赖短标签宽度，避免数值覆盖 Token 的末尾字形。 */
    const int32_t usage_title_width = 54;
    for (size_t i = 0; i < 2; ++i) {
        const int32_t usage_y = (landscape ? 54 : 58) + (int32_t)i * (landscape ? 14 : 18);
        lv_obj_set_pos(s_home.buddy_usage_titles[i], metric_inset, usage_y);
        lv_obj_set_size(s_home.buddy_usage_titles[i], usage_title_width, 14);
        lv_obj_set_pos(s_home.buddy_usage_labels[i], metric_inset + usage_title_width + 6, usage_y);
        lv_obj_set_size(s_home.buddy_usage_labels[i], metrics_width - usage_title_width - 6, 14);
        lv_obj_set_style_text_align(s_home.buddy_usage_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_home.buddy_usage_labels[i], LV_LABEL_LONG_DOT);
    }
    lv_obj_set_pos(s_home.buddy_empty_label, 8, 31);
    lv_obj_set_size(s_home.buddy_empty_label, LV_PCT(92), 18);
    lv_obj_set_pos(s_home.activity_label, 8, 6);
    lv_obj_set_size(s_home.activity_label, LV_PCT(92), LV_PCT(84));
}

void buddy_app_ui_home_apply_locale_fonts(void)
{
    const lv_font_t *meta = ui_locale_font(BUDDY_APP_FONT_META);

    if (s_home.page == NULL) return;
    lv_obj_set_style_text_font(s_home.date_label, meta, 0);
    lv_obj_set_style_text_font(s_home.transport_label, ui_locale_font(BUDDY_APP_FONT_BODY), 0);
    lv_obj_set_style_text_font(s_home.buddy_status_label, meta, 0);
    lv_obj_set_style_text_font(s_home.buddy_empty_label, meta, 0);
    for (size_t i = 0; i < 3; ++i) {
        lv_obj_set_style_text_font(s_home.buddy_metric_labels[i], BUDDY_APP_FONT_BODY, 0);
        lv_obj_set_style_text_font(s_home.buddy_metric_titles[i], meta, 0);
    }
    for (size_t i = 0; i < 2; ++i) {
        lv_obj_set_style_text_font(s_home.buddy_usage_labels[i], BUDDY_APP_FONT_META, 0);
        lv_obj_set_style_text_font(s_home.buddy_usage_titles[i], meta, 0);
    }
    lv_obj_set_style_text_font(s_home.activity_label, meta, 0);
}

void buddy_app_ui_home_refresh(const example_buddy_state_cache_t *state,
                               bool has_snapshot,
                               const char *title_text,
                               const char *date_text,
                               const char *transport_text,
                               const char *buddy_status_text,
                               lv_color_t buddy_status_color,
                               const char *tokens_text,
                               const char *context_text,
                               const char *activity_text)
{
    char total_text[24];
    char running_text[24];
    char waiting_text[24];

    if (state == NULL || s_home.page == NULL) {
        return;
    }
    const char *clock = title_text != NULL ? title_text : "";
    if (strcmp(lv_label_get_text(s_home.title_label), clock) != 0)
        buddy_app_ui_home_set_text(s_home.title_label, clock);
    const char *date = date_text != NULL ? date_text : "";
    if (strcmp(lv_label_get_text(s_home.date_label), date) != 0)
        buddy_app_ui_home_set_text(s_home.date_label, date);
    buddy_app_ui_home_set_text(s_home.transport_label, transport_text != NULL ? transport_text : "");
    lv_obj_set_style_text_color(s_home.transport_label, lv_color_hex(BUDDY_APP_COLOR_TEXT), 0);
    buddy_app_ui_home_set_text(s_home.buddy_status_label, buddy_status_text != NULL ? buddy_status_text : "");
    lv_obj_set_style_text_color(s_home.buddy_status_label, buddy_status_color, 0);
    if (has_snapshot) {
        buddy_app_ui_format_compact_u64(total_text, sizeof(total_text), state->total);
        buddy_app_ui_format_compact_u64(running_text, sizeof(running_text), state->running);
        buddy_app_ui_format_compact_u64(waiting_text, sizeof(waiting_text), state->waiting);
        lv_obj_add_flag(s_home.buddy_empty_label, LV_OBJ_FLAG_HIDDEN);
        for (size_t i = 0; i < 3; ++i) {
            lv_obj_clear_flag(s_home.buddy_metric_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_home.buddy_metric_titles[i], LV_OBJ_FLAG_HIDDEN);
        }
        for (size_t i = 0; i < 2; ++i) {
            lv_obj_clear_flag(s_home.buddy_usage_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_home.buddy_usage_titles[i], LV_OBJ_FLAG_HIDDEN);
        }
        buddy_app_ui_home_set_text(s_home.buddy_metric_labels[0], total_text);
        buddy_app_ui_home_set_text(s_home.buddy_metric_labels[1], running_text);
        buddy_app_ui_home_set_text(s_home.buddy_metric_labels[2], waiting_text);
        buddy_app_ui_home_set_text(s_home.buddy_metric_titles[0], ui_text(UI_TEXT_TOTAL));
        buddy_app_ui_home_set_text(s_home.buddy_metric_titles[1], ui_text(UI_TEXT_RUNNING));
        buddy_app_ui_home_set_text(s_home.buddy_metric_titles[2], ui_text(UI_TEXT_WAITING));
        buddy_app_ui_home_set_text(s_home.buddy_usage_titles[0], ui_text(UI_TEXT_TOKEN_TOTAL));
        buddy_app_ui_home_set_text(s_home.buddy_usage_titles[1], ui_text(UI_TEXT_CONTEXT));
        buddy_app_ui_home_set_usage_text(s_home.buddy_usage_labels[0], tokens_text);
        buddy_app_ui_home_set_usage_text(s_home.buddy_usage_labels[1], context_text);
    } else {
        lv_obj_clear_flag(s_home.buddy_empty_label, LV_OBJ_FLAG_HIDDEN);
        for (size_t i = 0; i < 3; ++i) {
            lv_obj_add_flag(s_home.buddy_metric_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_home.buddy_metric_titles[i], LV_OBJ_FLAG_HIDDEN);
            buddy_app_ui_home_set_text(s_home.buddy_metric_labels[i], "");
            buddy_app_ui_home_set_text(s_home.buddy_metric_titles[i], "");
        }
        for (size_t i = 0; i < 2; ++i) {
            lv_obj_add_flag(s_home.buddy_usage_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_home.buddy_usage_titles[i], LV_OBJ_FLAG_HIDDEN);
            buddy_app_ui_home_set_text(s_home.buddy_usage_labels[i], "");
            buddy_app_ui_home_set_text(s_home.buddy_usage_titles[i], "");
        }
        buddy_app_ui_home_set_text(s_home.buddy_empty_label, ui_text(UI_TEXT_NO_SESSION));
    }
    buddy_app_ui_home_set_text(s_home.activity_label, activity_text != NULL ? activity_text : "");
}
