#include <time.h>
/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_theme.h"

#include <stdio.h>

#include "ui_activity.h"
#include "ui_locale.h"


#if CONFIG_LV_FONT_MONTSERRAT_14
#define BUDDY_APP_UI_FONT_BODY (&lv_font_montserrat_14)
#else
#define BUDDY_APP_UI_FONT_BODY LV_FONT_DEFAULT
#endif

#if CONFIG_LV_FONT_MONTSERRAT_18
#define BUDDY_APP_UI_FONT_TITLE (&lv_font_montserrat_18)
#else
#define BUDDY_APP_UI_FONT_TITLE BUDDY_APP_UI_FONT_BODY
#endif

#if CONFIG_LV_FONT_MONTSERRAT_12
#define BUDDY_APP_UI_FONT_META (&lv_font_montserrat_12)
#else
#define BUDDY_APP_UI_FONT_META LV_FONT_DEFAULT
#endif

typedef struct {
    lv_obj_t *page;
    lv_obj_t *title_label;
    lv_obj_t *cards[BUDDY_APP_UI_ACTIVITY_VISIBLE];
    lv_obj_t *state_labels[BUDDY_APP_UI_ACTIVITY_VISIBLE];
    lv_obj_t *event_labels[BUDDY_APP_UI_ACTIVITY_VISIBLE];
    lv_obj_t *detail_labels[BUDDY_APP_UI_ACTIVITY_VISIBLE];
    lv_obj_t *time_labels[BUDDY_APP_UI_ACTIVITY_VISIBLE];
    lv_obj_t *empty_label;
} buddy_app_ui_activity_t;

static buddy_app_ui_activity_t s_activity;

static void buddy_app_ui_activity_style_label(lv_obj_t *label,
                                              const lv_font_t *font,
                                              lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static lv_obj_t *buddy_app_ui_activity_create_card(lv_obj_t *parent)
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

static void buddy_app_ui_activity_set_visible(lv_obj_t *object, bool visible)
{
    if (object == NULL) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static const char *buddy_app_ui_activity_source_name(buddy_app_activity_source_t source)
{
    switch (source) {
    case BUDDY_APP_ACTIVITY_SOURCE_BUDDY:
        return ui_text(UI_TEXT_BUDDY);
    case BUDDY_APP_ACTIVITY_SOURCE_BLE:
        return "BLE";
    case BUDDY_APP_ACTIVITY_SOURCE_PACK:
        return ui_text(UI_TEXT_SOURCE_PACK);
    case BUDDY_APP_ACTIVITY_SOURCE_SYSTEM:
        return ui_text(UI_TEXT_SOURCE_SYSTEM);
    default:
        return ui_text(UI_TEXT_SOURCE_UNKNOWN);
    }
}

const char *buddy_app_ui_activity_event_text(const buddy_app_activity_entry_t *entry)
{
    static const ui_text_id_t text_ids[] = {
        [BUDDY_APP_ACTIVITY_EVENT_SENSITIVE_OMITTED] = UI_TEXT_EVENT_SENSITIVE_OMITTED,
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_RECEIVED] = UI_TEXT_EVENT_PERMISSION_RECEIVED,
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_CLOSED] = UI_TEXT_EVENT_PERMISSION_CLOSED,
        [BUDDY_APP_ACTIVITY_EVENT_BUDDY_DISCONNECTED] = UI_TEXT_EVENT_BUDDY_DISCONNECTED,
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_APPROVED] = UI_TEXT_EVENT_PERMISSION_APPROVED,
        [BUDDY_APP_ACTIVITY_EVENT_PERMISSION_DENIED] = UI_TEXT_EVENT_PERMISSION_DENIED,
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_BACKEND_FAILED] = UI_TEXT_EVENT_PROMPT_BACKEND_FAILED,
        [BUDDY_APP_ACTIVITY_EVENT_BUDDY_PROTOCOL_ERROR] = UI_TEXT_EVENT_BUDDY_PROTOCOL_ERROR,
        [BUDDY_APP_ACTIVITY_EVENT_BLE_CONNECTED] = UI_TEXT_EVENT_BLE_CONNECTED,
        [BUDDY_APP_ACTIVITY_EVENT_BLE_DISCONNECTED] = UI_TEXT_EVENT_BLE_DISCONNECTED,
        [BUDDY_APP_ACTIVITY_EVENT_BLE_ENCRYPTED] = UI_TEXT_EVENT_BLE_ENCRYPTED,
        [BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALLED] = UI_TEXT_EVENT_PACK_INSTALLED,
        [BUDDY_APP_ACTIVITY_EVENT_PACK_INSTALL_FAILED] = UI_TEXT_EVENT_PACK_INSTALL_FAILED,
        [BUDDY_APP_ACTIVITY_EVENT_PACK_TRANSFER_STARTED] = UI_TEXT_EVENT_PACK_TRANSFER_STARTED,
        [BUDDY_APP_ACTIVITY_EVENT_PACK_CHANGED] = UI_TEXT_EVENT_PACK_CHANGED,
        [BUDDY_APP_ACTIVITY_EVENT_PACK_CLEARED] = UI_TEXT_EVENT_PACK_CLEARED,
        [BUDDY_APP_ACTIVITY_EVENT_SETTINGS_RESET] = UI_TEXT_EVENT_SETTINGS_RESET,
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_REPLY_IGNORED] = UI_TEXT_EVENT_PROMPT_REPLY_IGNORED,
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_QUEUE_FULL] = UI_TEXT_EVENT_PROMPT_QUEUE_FULL,
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_EXPIRED] = UI_TEXT_EVENT_PROMPT_EXPIRED,
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_FAILED] = UI_TEXT_EVENT_PROMPT_FAILED,
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_APPROVAL_QUEUED] = UI_TEXT_EVENT_PROMPT_APPROVAL_QUEUED,
        [BUDDY_APP_ACTIVITY_EVENT_PROMPT_DENIAL_QUEUED] = UI_TEXT_EVENT_PROMPT_DENIAL_QUEUED,
    };

    if (entry == NULL) {
        return "";
    }
    if (entry->event > BUDDY_APP_ACTIVITY_EVENT_GENERIC &&
        (size_t)entry->event < sizeof(text_ids) / sizeof(text_ids[0]) &&
        text_ids[entry->event] < UI_TEXT_COUNT) {
        return ui_text(text_ids[entry->event]);
    }
    return entry->parameter[0] != '\0' ? entry->parameter : entry->message;
}

bool buddy_app_ui_activity_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return false;
    }

    s_activity.page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_activity.page);
    lv_obj_set_size(s_activity.page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_activity.page, 0, 0);
    lv_obj_set_style_bg_opa(s_activity.page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_activity.page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_activity.title_label = lv_label_create(s_activity.page);
    buddy_app_ui_activity_style_label(s_activity.title_label, BUDDY_APP_UI_FONT_TITLE,
                                      lv_color_hex(BUDDY_APP_UI_COLOR_TEXT));
    lv_label_set_text(s_activity.title_label, ui_text(UI_TEXT_ACTIVITY));
    for (size_t i = 0; i < BUDDY_APP_UI_ACTIVITY_VISIBLE; ++i) {
        s_activity.cards[i] = buddy_app_ui_activity_create_card(s_activity.page);
        s_activity.state_labels[i] = lv_label_create(s_activity.cards[i]);
        buddy_app_ui_activity_style_label(s_activity.state_labels[i], BUDDY_APP_UI_FONT_BODY,
                                          lv_color_hex(BUDDY_APP_UI_COLOR_MUTED));
        s_activity.event_labels[i] = lv_label_create(s_activity.cards[i]);
        buddy_app_ui_activity_style_label(s_activity.event_labels[i], BUDDY_APP_UI_FONT_BODY,
                                          lv_color_hex(BUDDY_APP_UI_COLOR_TEXT));
        lv_label_set_long_mode(s_activity.event_labels[i], LV_LABEL_LONG_DOT);
        s_activity.detail_labels[i] = lv_label_create(s_activity.cards[i]);
        buddy_app_ui_activity_style_label(s_activity.detail_labels[i], BUDDY_APP_UI_FONT_META,
                                          lv_color_hex(BUDDY_APP_UI_COLOR_MUTED));
        lv_label_set_long_mode(s_activity.detail_labels[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(s_activity.detail_labels[i], 1, 0);
        s_activity.time_labels[i] = lv_label_create(s_activity.cards[i]);
        buddy_app_ui_activity_style_label(s_activity.time_labels[i], BUDDY_APP_UI_FONT_META,
                                          lv_color_hex(BUDDY_APP_UI_COLOR_MUTED));
        lv_obj_set_style_text_align(s_activity.time_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_add_flag(s_activity.cards[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_activity.empty_label = lv_label_create(s_activity.page);
    buddy_app_ui_activity_style_label(s_activity.empty_label, BUDDY_APP_UI_FONT_BODY,
                                      lv_color_hex(BUDDY_APP_UI_COLOR_MUTED));
    lv_obj_set_style_text_align(s_activity.empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_activity.empty_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_activity.empty_label, ui_text(UI_TEXT_NO_RECENT_EVENTS));
    return true;
}

lv_obj_t *buddy_app_ui_activity_root(void)
{
    return s_activity.page;
}

void buddy_app_ui_activity_layout(bool landscape,
                                  int32_t content_width,
                                  int32_t content_height,
                                  int32_t inset,
                                  int32_t top,
                                  int32_t card_width)
{
    int32_t activity_y = top + 30;
    int32_t activity_bottom = content_height - 8;
    int32_t activity_gap = 6;
    int32_t activity_h = landscape ? activity_bottom - activity_y :
                         (activity_bottom - activity_y - activity_gap * 2) / 3;
    int32_t activity_w = landscape ? (card_width - activity_gap * 2) / 3 : card_width;

    if (s_activity.page == NULL) {
        return;
    }
    lv_obj_set_size(s_activity.page, content_width, content_height);
    lv_obj_set_pos(s_activity.page, 0, 0);
    lv_obj_set_pos(s_activity.title_label, inset, top);
    lv_obj_set_width(s_activity.title_label, card_width);
    for (size_t i = 0; i < BUDDY_APP_UI_ACTIVITY_VISIBLE; ++i) {
        lv_obj_set_pos(s_activity.cards[i],
                       landscape ? inset + (int32_t)i * (activity_w + activity_gap) : inset,
                       landscape ? activity_y : activity_y + (int32_t)i * (activity_h + activity_gap));
        lv_obj_set_size(s_activity.cards[i], activity_w, activity_h);
        lv_obj_set_pos(s_activity.state_labels[i], 8, 8);
        lv_obj_set_size(s_activity.state_labels[i], 12, 16);
        lv_obj_set_pos(s_activity.event_labels[i], landscape ? 24 : 26, 6);
        lv_obj_set_width(s_activity.event_labels[i], landscape ? activity_w - 32 : LV_PCT(62));
        lv_obj_set_pos(s_activity.detail_labels[i], landscape ? 8 : 26, 24);
        lv_obj_set_size(s_activity.detail_labels[i],
                        landscape ? activity_w - 16 : LV_PCT(62),
                        landscape ? activity_h - 32 : LV_PCT(48));
        if (landscape) {
            lv_obj_add_flag(s_activity.time_labels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_activity.time_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(s_activity.time_labels[i], LV_ALIGN_BOTTOM_RIGHT, -8, -6);
            lv_obj_set_width(s_activity.time_labels[i], LV_PCT(30));
        }
    }
    lv_obj_set_size(s_activity.empty_label, card_width, activity_bottom - activity_y);
    lv_obj_set_pos(s_activity.empty_label, inset, activity_y);
}

void buddy_app_ui_activity_apply_locale_fonts(void)
{
    if (s_activity.page == NULL) {
        return;
    }
    lv_obj_set_style_text_font(s_activity.title_label, ui_locale_font(BUDDY_APP_UI_FONT_TITLE), 0);
    lv_obj_set_style_text_font(s_activity.empty_label, ui_locale_font(BUDDY_APP_UI_FONT_BODY), 0);
    for (size_t i = 0; i < BUDDY_APP_UI_ACTIVITY_VISIBLE; ++i) {
        lv_obj_set_style_text_font(s_activity.state_labels[i], ui_locale_font(BUDDY_APP_UI_FONT_BODY), 0);
        lv_obj_set_style_text_font(s_activity.event_labels[i], ui_locale_font(BUDDY_APP_UI_FONT_BODY), 0);
        lv_obj_set_style_text_font(s_activity.detail_labels[i], ui_locale_font(BUDDY_APP_UI_FONT_META), 0);
        lv_obj_set_style_text_font(s_activity.time_labels[i], ui_locale_font(BUDDY_APP_UI_FONT_META), 0);
    }
}

void buddy_app_ui_activity_refresh(buddy_app_t *app)
{
    buddy_app_activity_entry_t entries[BUDDY_APP_UI_ACTIVITY_VISIBLE];
    size_t count;

    if (app == NULL || s_activity.page == NULL) {
        return;
    }
    lv_label_set_text(s_activity.title_label, ui_text(UI_TEXT_ACTIVITY));
    count = buddy_app_activity_snapshot_newest(app, entries, BUDDY_APP_UI_ACTIVITY_VISIBLE);
    buddy_app_ui_activity_set_visible(s_activity.empty_label, count == 0);
    for (size_t i = 0; i < BUDDY_APP_UI_ACTIVITY_VISIBLE; ++i) {
        bool visible = i < count;

        buddy_app_ui_activity_set_visible(s_activity.cards[i], visible);
        if (!visible) {
            continue;
        }
        const buddy_app_activity_entry_t *entry = &entries[i];
        const char *state = entry->level == BUDDY_APP_ACTIVITY_LEVEL_ERROR ? "!" :
                            entry->level == BUDDY_APP_ACTIVITY_LEVEL_WARNING ? "!" : "i";
        const char *event_token = entry->parameter[0] != '\0'
                                      ? entry->parameter
                                      : (entry->message[0] != '\0' ? entry->message : "event");
        char elapsed[20];
        char detail[96];

        if (entry->wall_time_seconds != 0) {
            time_t event_time = entry->wall_time_seconds + entry->tz_offset_seconds;
            struct tm tm = {0};
            gmtime_r(&event_time, &tm);
            strftime(elapsed, sizeof(elapsed), "%m-%d %H:%M:%S", &tm);
        } else {
            snprintf(elapsed, sizeof(elapsed), "T+%lus", (unsigned long)entry->uptime_seconds);
        }
        lv_label_set_text(s_activity.state_labels[i], state);
        lv_obj_set_style_text_color(s_activity.state_labels[i],
                                    lv_color_hex(entry->level == BUDDY_APP_ACTIVITY_LEVEL_ERROR ?
                                                     BUDDY_APP_UI_COLOR_DENY :
                                                     entry->level == BUDDY_APP_ACTIVITY_LEVEL_WARNING ?
                                                     BUDDY_APP_UI_COLOR_ACCENT :
                                                     BUDDY_APP_UI_COLOR_MUTED),
                                    0);
        snprintf(detail,
                 sizeof(detail),
                 "%s: %s | %s",
                 ui_locale_get() == UI_LOCALE_EN_US ? "Source" : "来源",
                 buddy_app_ui_activity_source_name(entry->source),
                 event_token);
        lv_label_set_text(s_activity.event_labels[i], buddy_app_ui_activity_event_text(entry));
        lv_label_set_text(s_activity.detail_labels[i], detail);
        lv_label_set_text(s_activity.time_labels[i], elapsed);
    }
}
