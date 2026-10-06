/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_theme.h"

#include "ui_prompt.h"
#include "ui_locale.h"
#include <string.h>


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
#define BUDDY_APP_FONT_ACTION BUDDY_APP_FONT_BODY
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

static struct {
    lv_obj_t *page, *card, *title_label, *body_scroll, *body_label, *detail_label;
    lv_obj_t *allow_card, *allow_label, *deny_card, *deny_label;
    int32_t card_width, card_height;
    bool passkey_active;
    bool attention_anim_running;
} s_prompt;

static void style_label(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static void style_card(lv_obj_t *obj, uint32_t bg_color, uint32_t border_color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, BUDDY_APP_UI_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border_color), 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_outline_color(obj, lv_color_hex(BUDDY_APP_COLOR_ACCENT), 0);
    lv_obj_set_style_outline_opa(obj, LV_OPA_60, 0);
}

static void layout_card_text(void)
{
    const bool show_detail = lv_label_get_text(s_prompt.detail_label)[0] != '\0';
    const int32_t detail_height = show_detail ? 44 : 0;
    const int32_t body_height = s_prompt.card_height - 20 - detail_height - (show_detail ? 4 : 0);

    lv_obj_set_pos(s_prompt.body_scroll, 10, 10);
    lv_obj_set_size(s_prompt.body_scroll, s_prompt.card_width - 20, body_height);
    lv_obj_set_width(s_prompt.body_label, s_prompt.card_width - 24);
    lv_obj_set_height(s_prompt.body_label, LV_SIZE_CONTENT);
    if (show_detail) {
        lv_obj_clear_flag(s_prompt.detail_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_prompt.detail_label, 10, s_prompt.card_height - 10 - detail_height);
        lv_obj_set_size(s_prompt.detail_label, s_prompt.card_width - 20, detail_height);
    } else {
        lv_obj_add_flag(s_prompt.detail_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *create_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);

    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static void anim_border_width_cb(void *obj, int32_t value)
{
    lv_obj_set_style_outline_width((lv_obj_t *)obj, value, 0);
}

bool buddy_app_ui_prompt_create(lv_obj_t *parent,
                                const buddy_app_ui_prompt_callbacks_t *callbacks)
{
    if (s_prompt.page != NULL)
        return true;
    if (parent == NULL || callbacks == NULL || callbacks->track_tap_event == NULL ||
        callbacks->action_event == NULL)
        return false;
    s_prompt.page = create_page(parent);
    if (s_prompt.page == NULL)
        return false;
    lv_obj_set_style_bg_color(s_prompt.page, lv_color_hex(BUDDY_APP_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_prompt.page, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_prompt.page, LV_OBJ_FLAG_CLICKABLE);

    s_prompt.title_label = lv_label_create(s_prompt.page);
    style_label(s_prompt.title_label, BUDDY_APP_FONT_TITLE, lv_color_hex(BUDDY_APP_COLOR_ACCENT));
    lv_label_set_long_mode(s_prompt.title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_prompt.title_label, ui_text(UI_TEXT_WAITING));
    s_prompt.card = lv_obj_create(s_prompt.page);
    lv_obj_set_size(s_prompt.card, 44, 44);
    style_card(s_prompt.card, BUDDY_APP_COLOR_PANEL, BUDDY_APP_COLOR_PANEL_ALT);
    lv_obj_clear_flag(s_prompt.card, LV_OBJ_FLAG_SCROLLABLE);

    /* 只让审批说明滚动，标题、状态和操作按钮保持固定。 */
    s_prompt.body_scroll = lv_obj_create(s_prompt.card);
    lv_obj_remove_style_all(s_prompt.body_scroll);
    lv_obj_add_flag(s_prompt.body_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_prompt.body_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_prompt.body_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(s_prompt.body_scroll, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_prompt.body_scroll, lv_color_hex(BUDDY_APP_COLOR_MUTED),
                              LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_prompt.body_scroll, LV_OPA_COVER, LV_PART_SCROLLBAR);
    s_prompt.body_label = lv_label_create(s_prompt.body_scroll);
    style_label(s_prompt.body_label, BUDDY_APP_FONT_BODY, lv_color_hex(BUDDY_APP_COLOR_TEXT));
    lv_label_set_long_mode(s_prompt.body_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_prompt.body_label, 4, 0);
    lv_label_set_text(s_prompt.body_label, ui_text(UI_TEXT_NO_PENDING_REQUEST));
    s_prompt.detail_label = lv_label_create(s_prompt.card);
    style_label(s_prompt.detail_label, BUDDY_APP_FONT_META, lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_label_set_long_mode(s_prompt.detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_prompt.detail_label, 2, 0);
    lv_label_set_text(s_prompt.detail_label, "");

    s_prompt.allow_card = lv_obj_create(s_prompt.page);
    lv_obj_set_pos(s_prompt.allow_card, 0, 0);
    lv_obj_set_size(s_prompt.allow_card, 44, 44);
    style_card(s_prompt.allow_card, BUDDY_APP_COLOR_ALLOW_BG, BUDDY_APP_COLOR_ALLOW);
    lv_obj_set_style_bg_color(s_prompt.allow_card, lv_color_hex(BUDDY_APP_COLOR_ALLOW_BG),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_prompt.allow_card, 3, LV_STATE_PRESSED);
    lv_obj_set_style_opa(s_prompt.allow_card, LV_OPA_40, LV_STATE_DISABLED);
    s_prompt.allow_label = lv_label_create(s_prompt.allow_card);
    lv_obj_center(s_prompt.allow_label);
    style_label(s_prompt.allow_label, BUDDY_APP_FONT_ACTION, lv_color_hex(BUDDY_APP_COLOR_TEXT));
    lv_label_set_text(s_prompt.allow_label, ui_text(UI_TEXT_ALLOW_ONCE));
    lv_obj_add_flag(s_prompt.allow_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_prompt.allow_card, callbacks->track_tap_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_prompt.allow_card, callbacks->action_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)callbacks->allow_action);

    s_prompt.deny_card = lv_obj_create(s_prompt.page);
    lv_obj_set_pos(s_prompt.deny_card, 0, 0);
    lv_obj_set_size(s_prompt.deny_card, 44, 44);
    style_card(s_prompt.deny_card, BUDDY_APP_COLOR_DENY_BG, BUDDY_APP_COLOR_DENY);
    lv_obj_set_style_bg_color(s_prompt.deny_card, lv_color_hex(BUDDY_APP_COLOR_DENY_BG), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_prompt.deny_card, 3, LV_STATE_PRESSED);
    lv_obj_set_style_opa(s_prompt.deny_card, LV_OPA_40, LV_STATE_DISABLED);
    s_prompt.deny_label = lv_label_create(s_prompt.deny_card);
    lv_obj_center(s_prompt.deny_label);
    style_label(s_prompt.deny_label, BUDDY_APP_FONT_ACTION, lv_color_hex(BUDDY_APP_COLOR_TEXT));
    lv_label_set_text(s_prompt.deny_label, ui_text(UI_TEXT_DENY));
    lv_obj_add_flag(s_prompt.deny_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_prompt.deny_card, callbacks->track_tap_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_prompt.deny_card, callbacks->action_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)callbacks->deny_action);
    return true;
}

lv_obj_t *buddy_app_ui_prompt_root(void) { return s_prompt.page; }

void buddy_app_ui_prompt_layout(int32_t screen_width, int32_t screen_height, int32_t inset, int32_t gap)
{
    const int32_t prompt_top = 12;
    int32_t prompt_width;
    int32_t action_width;

    if (s_prompt.page == NULL)
        return;
    prompt_width = screen_width - inset * 2;
    action_width = (prompt_width - gap) / 2;
    s_prompt.card_width = prompt_width;
    s_prompt.card_height = screen_height - prompt_top - 30 - 62;
    lv_obj_set_size(s_prompt.page, screen_width, screen_height);
    lv_obj_set_pos(s_prompt.page, 0, 0);
    lv_obj_set_pos(s_prompt.title_label, inset, prompt_top);
    lv_obj_set_width(s_prompt.title_label, prompt_width);
    lv_obj_set_pos(s_prompt.card, inset, prompt_top + 30);
    lv_obj_set_size(s_prompt.card, s_prompt.card_width, s_prompt.card_height);
    lv_obj_set_pos(s_prompt.allow_card, inset, screen_height - 54);
    lv_obj_set_size(s_prompt.allow_card, action_width, 46);
    lv_obj_set_pos(s_prompt.deny_card, inset + action_width + gap, screen_height - 54);
    lv_obj_set_size(s_prompt.deny_card, action_width, 46);
    layout_card_text();
}

void buddy_app_ui_prompt_show(void)
{
    if (s_prompt.page != NULL)
        lv_obj_clear_flag(s_prompt.page, LV_OBJ_FLAG_HIDDEN);
}

void buddy_app_ui_prompt_hide(void)
{
    if (s_prompt.page != NULL)
        lv_obj_add_flag(s_prompt.page, LV_OBJ_FLAG_HIDDEN);
}

void buddy_app_ui_prompt_apply_locale_fonts(void)
{
    if (s_prompt.page == NULL)
        return;
    lv_obj_set_style_text_font(s_prompt.title_label, ui_locale_font(BUDDY_APP_FONT_TITLE), 0);
    lv_obj_set_style_text_font(s_prompt.body_label,
                               s_prompt.passkey_active ? BUDDY_APP_FONT_PASSKEY :
                                                         ui_locale_content_font(), 0);
    lv_obj_set_style_text_font(s_prompt.detail_label, ui_locale_font(BUDDY_APP_FONT_META), 0);
    lv_obj_set_style_text_font(s_prompt.allow_label, ui_locale_font(BUDDY_APP_FONT_ACTION), 0);
    lv_obj_set_style_text_font(s_prompt.deny_label, ui_locale_font(BUDDY_APP_FONT_ACTION), 0);
}

void buddy_app_ui_prompt_refresh(bool passkey_active, bool prompt_active,
                                 const char *title, const char *body, const char *detail,
                                 bool reset_scroll)
{
    const char *shown_body = body != NULL ? body : "";
    const char *shown_detail = detail != NULL ? detail : "";
    bool body_changed;
    bool detail_changed;

    if (s_prompt.page == NULL)
        return;
    s_prompt.passkey_active = passkey_active;
    if (strcmp(lv_label_get_text(s_prompt.title_label), title != NULL ? title : "") != 0)
        lv_label_set_text(s_prompt.title_label, title != NULL ? title : "");
    body_changed = strcmp(lv_label_get_text(s_prompt.body_label), shown_body) != 0;
    if (body_changed)
        lv_label_set_text(s_prompt.body_label, shown_body);
    detail_changed = strcmp(lv_label_get_text(s_prompt.detail_label), shown_detail) != 0;
    if (detail_changed)
        lv_label_set_text(s_prompt.detail_label, shown_detail);
    if (detail_changed)
        layout_card_text();
    if (body_changed || reset_scroll)
        lv_obj_scroll_to_y(s_prompt.body_scroll, 0, LV_ANIM_OFF);
    lv_label_set_text(s_prompt.allow_label, ui_text(UI_TEXT_ALLOW_ONCE));
    lv_label_set_text(s_prompt.deny_label, ui_text(UI_TEXT_DENY));
    lv_obj_set_style_text_font(s_prompt.body_label, passkey_active ? BUDDY_APP_FONT_PASSKEY :
                               ui_locale_content_font(), 0);
    style_card(s_prompt.card, BUDDY_APP_COLOR_PANEL, passkey_active ? BUDDY_APP_COLOR_PASSKEY :
               (prompt_active ? BUDDY_APP_COLOR_ACCENT : BUDDY_APP_COLOR_PANEL_ALT));
}

void buddy_app_ui_prompt_set_action_state(bool prompt_active, bool reply_submitted)
{
    if (s_prompt.page == NULL)
        return;
    style_card(s_prompt.allow_card, BUDDY_APP_COLOR_ALLOW_BG, BUDDY_APP_COLOR_ALLOW);
    style_card(s_prompt.deny_card, BUDDY_APP_COLOR_DENY_BG, BUDDY_APP_COLOR_DENY);
    lv_obj_set_style_text_color(s_prompt.allow_label, lv_color_hex(BUDDY_APP_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_prompt.deny_label, lv_color_hex(BUDDY_APP_COLOR_TEXT), 0);
    if (prompt_active) {
        lv_obj_clear_flag(s_prompt.allow_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_prompt.deny_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_prompt.allow_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_prompt.deny_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (reply_submitted) {
        lv_obj_add_state(s_prompt.allow_card, LV_STATE_DISABLED);
        lv_obj_add_state(s_prompt.deny_card, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_prompt.allow_card, LV_STATE_DISABLED);
        lv_obj_remove_state(s_prompt.deny_card, LV_STATE_DISABLED);
    }
}

void buddy_app_ui_prompt_set_attention_anim(bool active)
{
    if (s_prompt.card == NULL || active == s_prompt.attention_anim_running)
        return;
    if (active) {
        lv_anim_t anim;

        lv_anim_init(&anim);
        lv_anim_set_var(&anim, s_prompt.card);
        lv_anim_set_values(&anim, 0, 3);
        lv_anim_set_duration(&anim, 520);
        lv_anim_set_playback_duration(&anim, 520);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&anim, anim_border_width_cb);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        lv_anim_start(&anim);
    } else {
        lv_anim_del(s_prompt.card, anim_border_width_cb);
        lv_obj_set_style_outline_width(s_prompt.card, 0, 0);
    }
    s_prompt.attention_anim_running = active;
}
