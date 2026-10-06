/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include "ui_settings.h"
#include "ui_theme.h"
#include "ui_locale.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "esp_attr.h"

#define BUDDY_APP_FONT_BODY (&lv_font_montserrat_14)
#define BUDDY_APP_FONT_META (&lv_font_montserrat_12)

typedef struct {
    lv_obj_t *page, *menu, *root_page, *pages[6], *root_buttons[6];
    lv_obj_t *pack_caption_label, *pack_detail_label, *pack_selector, *pack_status_label;
    lv_obj_t *sliders[3], *values[3], *aod_switch, *dark_switch, *theme_buttons[2];
    lv_obj_t *language, *name_button, *name_value, *name_status, *reset_button, *diag_buttons[3];
    lv_obj_t *time_status, *maintenance;
    lv_obj_t *name_dialog, *name_title, *name_ta, *keyboard, *name_save, *name_cancel;
    lv_obj_t *reset_dialog, *reset_body, *reset_confirm, *reset_cancel;
} settings_state_t;
static settings_state_t s_state;
static buddy_app_t *s_app;
static buddy_app_ui_settings_callbacks_t s_callbacks;
static bool s_syncing, s_keyboard_shift, s_keyboard_landscape;
static uint8_t s_keyboard_page;
static char s_keyboard_labels[20][8];
static const char *s_keyboard_map[25];
static bool settings_allowed(void) { return s_callbacks.interaction_allowed(s_callbacks.context); }
static bool settings_click(lv_event_t *event) { return s_callbacks.tap_is_click(event, s_callbacks.context); }
static const char *settings_text(const char *en, const char *zh) { return ui_locale_get() == UI_LOCALE_ZH_CN ? zh : en; }
static void settings_save(void) { s_callbacks.request_save(s_callbacks.context); }
static void button_text(lv_obj_t *obj, const char *text) { lv_label_set_text(lv_obj_get_child(obj, 0), text); }

static void settings_style_label(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static lv_obj_t *settings_menu_button(lv_obj_t *parent, const char *text,
                                      lv_event_cb_t callback, void *user_data,
                                      lv_event_cb_t track_tap_event)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_set_size(button, LV_PCT(100), 44);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(BUDDY_APP_COLOR_PANEL), 0);
    lv_label_set_text(label, text);
    settings_style_label(label, BUDDY_APP_FONT_BODY, lv_color_hex(BUDDY_APP_COLOR_TEXT));
    lv_obj_set_width(label, LV_PCT(92));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, track_tap_event, LV_EVENT_ALL, NULL);
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

static void settings_style_list_button(lv_obj_t *button)
{
    /* 列表行只有底部分隔线，名称与进入箭头各自占位。 */
    lv_obj_t *label = lv_obj_get_child(button, 0);
    lv_obj_set_style_radius(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(BUDDY_APP_COLOR_ACCENT_SOFT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_side(button, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(BUDDY_APP_COLOR_SEPARATOR), 0);
    lv_obj_set_width(label, LV_PCT(84));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_t *arrow = lv_label_create(button);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, lv_color_hex(BUDDY_APP_COLOR_MUTED), 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
}
static lv_obj_t *settings_slider_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 44);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, LV_PCT(100), 6);
    /* 百分比宽度基于下方父容器的内容区，扣除固定端点空白。 */
    lv_obj_set_style_pad_left(row, 22, 0);
    lv_obj_set_style_pad_right(row, 22, 0);
    lv_obj_center(slider);
    lv_obj_set_ext_click_area(slider, 19);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(BUDDY_APP_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(BUDDY_APP_COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    return slider;
}
static void settings_keyboard_map_update(void)
{
    static const char *const chars[] = {
        "qwertyuiopasd", "fghjklzxcvbnm",
        "1234567890!?#", "$%&*+=/\\:;,()",
        "[]{}<>|~^'\"_@",
    };
    const char *keys = chars[s_keyboard_page];
    size_t pos = 0;
    for (size_t i = 0; i < 20; ++i) {
        if (i < 13) {
            char ch = keys[i];
            if (s_keyboard_page < 2 && s_keyboard_shift && ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 'a' + 'A');
            s_keyboard_labels[i][0] = ch;
            s_keyboard_labels[i][1] = '\0';
        } else {
            const char *control = i == 13 ? LV_SYMBOL_RIGHT :
                                  i == 14 ? (s_keyboard_page < 2 ? "123" : "ABC") :
                                  i == 15 ? (s_keyboard_page < 2 ? "Shift" : "`") : i == 16 ? "Space" :
                                  i == 17 ? "Del" : i == 18 ? "." : "-";
            lv_strlcpy(s_keyboard_labels[i], control, sizeof(s_keyboard_labels[i]));
        }
        s_keyboard_map[pos++] = s_keyboard_labels[i];
        if (i != 19 && (s_keyboard_landscape ? ((i + 1) % 7 == 0) :
                                               ((i + 1) % 5 == 0)))
            s_keyboard_map[pos++] = "\n";
    }
    s_keyboard_map[pos] = "";
    lv_buttonmatrix_set_map(s_state.keyboard, s_keyboard_map);
}
static void settings_keyboard(lv_event_t *event)
{
    uint32_t index = lv_buttonmatrix_get_selected_button(s_state.keyboard);
    const char *key = lv_buttonmatrix_get_button_text(s_state.keyboard, index);
    (void)event;
    if (key == NULL || !settings_allowed()) return;
    if (strcmp(key, LV_SYMBOL_RIGHT) == 0) {
        s_keyboard_page = s_keyboard_page < 2 ? (uint8_t)(1 - s_keyboard_page) :
                                                  (uint8_t)(2 + (s_keyboard_page - 1) % 3);
        settings_keyboard_map_update();
    } else if (strcmp(key, "123") == 0 || strcmp(key, "ABC") == 0) {
        s_keyboard_page = s_keyboard_page < 2 ? 2 : 0;
        settings_keyboard_map_update();
    } else if (strcmp(key, "Shift") == 0) {
        s_keyboard_shift = !s_keyboard_shift;
        settings_keyboard_map_update();
    } else if (strcmp(key, "Space") == 0) lv_textarea_add_char(s_state.name_ta, ' ');
    else if (strcmp(key, "Del") == 0) lv_textarea_delete_char(s_state.name_ta);
    else lv_textarea_add_text(s_state.name_ta, key);
    if (s_callbacks.input_activity != NULL) s_callbacks.input_activity(s_callbacks.context);
}

void buddy_app_ui_settings_close_name_dialog(void)
{
    if (s_state.name_dialog != NULL) {
        lv_obj_add_flag(s_state.name_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(s_state.name_ta, "");
    }
}
void buddy_app_ui_settings_close_reset_dialog(void)
{
    if (s_state.reset_dialog != NULL) lv_obj_add_flag(s_state.reset_dialog, LV_OBJ_FLAG_HIDDEN);
}
static void settings_open_page(lv_event_t *event)
{
    if (settings_click(event) && settings_allowed()) lv_menu_set_page(s_state.menu, lv_event_get_user_data(event));
}
static void settings_action(lv_event_t *event)
{
    if (!settings_click(event) || !settings_allowed()) return;
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    static EXT_RAM_BSS_ATTR buddy_app_ui_snapshot_t snapshot;
    if (!buddy_app_ui_snapshot_get(s_app, &snapshot)) return;
    switch (action) {
    case 0: case 1:
        s_callbacks.request_theme((buddy_app_theme_t)(action + (snapshot.theme >= 2 ? 2 : 0)), s_callbacks.context);
        break;
    case 4:
        lv_textarea_set_text(s_state.name_ta, snapshot.ble_device_name);
        s_keyboard_page = 0; s_keyboard_shift = false; settings_keyboard_map_update();
        lv_obj_clear_flag(s_state.name_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_state.name_dialog);
        break;
    case 5:
        lv_obj_clear_flag(s_state.reset_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_state.reset_dialog);
        break;
    case 6: case 7:
        s_callbacks.request_rotation(action == 6 ? BUDDY_APP_ROTATION_PORTRAIT : BUDDY_APP_ROTATION_LANDSCAPE, s_callbacks.context);
        break;
    case 9: s_callbacks.open_diagnostics(s_callbacks.context); break;
    case 10: {
        esp_err_t err = s_callbacks.request_reset(s_callbacks.context);
        if (err == ESP_OK) buddy_app_ui_settings_close_reset_dialog();
        else lv_label_set_text(s_state.reset_body, esp_err_to_name(err));
        break;
    }
    case 11: buddy_app_ui_settings_close_reset_dialog(); break;
    case 12: {
        esp_err_t err = buddy_app_settings_set_device_name(s_app, true, lv_textarea_get_text(s_state.name_ta));
        if (err == ESP_OK) buddy_app_ui_settings_close_name_dialog();
        else lv_label_set_text(s_state.name_title, settings_text("Invalid name / save unavailable", "名称无效或暂时无法保存"));
        break;
    }
    case 13: buddy_app_ui_settings_close_name_dialog(); break;
    }
}
/* 快照刷新不派发操作；用户拖动时才提交，持应用锁时不调用 LVGL。 */
static void settings_value(lv_event_t *event)
{
    if (s_syncing || !settings_allowed()) return;
    uintptr_t id = (uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target_obj(event);
    if (id == 3) {
        ui_locale_set(lv_dropdown_get_selected(target) == 0 ? UI_LOCALE_ZH_CN : UI_LOCALE_EN_US);
        s_callbacks.language_changed(s_callbacks.context); settings_save(); return;
    }
    if (id == 4) {
        static EXT_RAM_BSS_ATTR buddy_app_ui_snapshot_t snapshot;
        if (!buddy_app_ui_snapshot_get(s_app, &snapshot)) return;
        s_callbacks.request_theme((buddy_app_theme_t)(snapshot.theme % 2 +
            (lv_obj_has_state(target, LV_STATE_CHECKED) ? 2 : 0)), s_callbacks.context); return;
    }
    uint32_t value = id == 5 ? lv_obj_has_state(target, LV_STATE_CHECKED) : lv_slider_get_value(target);
    xSemaphoreTake(s_app->mutex, portMAX_DELAY);
    if (id == 0) s_app->display_brightness_percent = value;
    else if (id == 1) s_app->aod_timeout_minutes = value;
    else if (id == 2) s_app->aod_brightness_percent = value;
    else if (id == 5) s_app->aod_enabled = value != 0;
    xSemaphoreGive(s_app->mutex);
    if (id == 0) s_callbacks.set_brightness(value, s_callbacks.context);
    settings_save();
    s_callbacks.notify(BUDDY_APP_UI_DIRTY_SETTINGS, s_callbacks.context);
}
static lv_obj_t *action_button(lv_obj_t *parent, const char *text, uintptr_t action)
{
    return settings_menu_button(parent, text, settings_action, (void *)action, s_callbacks.track_tap_event);
}
static lv_obj_t *plain_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    settings_style_label(label, ui_locale_font(BUDDY_APP_FONT_BODY), lv_color_hex(BUDDY_APP_COLOR_TEXT));
    return label;
}
static lv_obj_t *switch_row(lv_obj_t *parent, const char *text, uintptr_t id)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 44);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(row); lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sw = lv_switch_create(row); lv_obj_set_size(sw, 64, 44);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(sw, settings_value, LV_EVENT_VALUE_CHANGED, (void *)id);
    return sw;
}
lv_obj_t *buddy_app_ui_settings_root(void) { return s_state.page; }
bool buddy_app_ui_settings_create(lv_obj_t *parent, buddy_app_t *app,
                                  const buddy_app_ui_settings_callbacks_t *callbacks)
{
    if (parent == NULL || app == NULL || callbacks == NULL) return false;
    s_app = app; s_callbacks = *callbacks;
    s_state.page = lv_obj_create(parent); lv_obj_remove_style_all(s_state.page);
    lv_obj_clear_flag(s_state.page, LV_OBJ_FLAG_SCROLLABLE);
    s_state.menu = lv_menu_create(s_state.page);
    /* 设置页透出屏幕画布，避免 menu 默认卡片底色形成内嵌矩形。 */
    lv_obj_set_style_bg_opa(s_state.menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_state.menu, 0, 0);
    lv_obj_set_style_shadow_width(s_state.menu, 0, 0);
    lv_obj_t *header = lv_menu_get_main_header(s_state.menu);
    lv_obj_set_style_bg_opa(lv_obj_get_parent(header), LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_t *back = lv_menu_get_main_header_back_button(s_state.menu);
    lv_obj_set_size(header, LV_PCT(100), 46);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_gap(header, 6, 0);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_move_to_index(back, 1);
    lv_obj_set_size(back, 44, 44);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(BUDDY_APP_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back, BUDDY_APP_UI_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), 0);
    lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_color(lv_obj_get_child(back, 0), lv_color_hex(BUDDY_APP_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), LV_STATE_PRESSED);
    s_state.root_page = lv_menu_page_create(s_state.menu, "Settings");
    const char *titles[] = {"Display", "AOD", "Character packs", "Time", "Device", "Diagnostics"};
    for (size_t i = 0; i < 6; ++i) {
        s_state.pages[i] = lv_menu_page_create(s_state.menu, titles[i]);
        s_state.root_buttons[i] = settings_menu_button(s_state.root_page, titles[i], settings_open_page,
                                                      s_state.pages[i], callbacks->track_tap_event);
        settings_style_list_button(s_state.root_buttons[i]);
    }
    for (size_t i = 0; i < 3; ++i) {
        s_state.values[i] = plain_label(s_state.pages[i == 0 ? 0 : 1]);
        s_state.sliders[i] = settings_slider_create(s_state.pages[i == 0 ? 0 : 1]);
        lv_slider_set_range(s_state.sliders[i], 1, i == 1 ? 180 : 100);
        lv_obj_add_event_cb(s_state.sliders[i], settings_value, LV_EVENT_VALUE_CHANGED, (void *)i);
    }
    s_state.theme_buttons[0] = action_button(s_state.pages[0], "DeepSeek", 0);
    s_state.theme_buttons[1] = action_button(s_state.pages[0], "Claude", 1);
    /* 品牌按钮并排；显示页固定 156px 内容高度，横屏也不需要滚动。 */
    lv_obj_t *theme_row = lv_obj_create(s_state.pages[0]);
    lv_obj_remove_style_all(theme_row);
    lv_obj_set_size(theme_row, LV_PCT(100), 44);
    lv_obj_set_flex_flow(theme_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(theme_row, 6, 0);
    for (size_t i = 0; i < 2; ++i) {
        lv_obj_t *button = s_state.theme_buttons[i];
        lv_obj_set_parent(button, theme_row);
        lv_obj_set_width(button, 0);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_pad_hor(button, 4, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(BUDDY_APP_COLOR_ACCENT_SOFT), LV_STATE_CHECKED);
        lv_obj_set_style_border_width(button, BUDDY_APP_UI_BORDER_WIDTH, LV_STATE_CHECKED);
        lv_obj_set_style_border_color(button, lv_color_hex(BUDDY_APP_COLOR_ACCENT), LV_STATE_CHECKED);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    }
    s_state.dark_switch = switch_row(s_state.pages[0], "Dark", 4);
    lv_obj_set_style_pad_all(s_state.pages[0], 0, 0);
    lv_obj_set_style_pad_row(s_state.pages[0], 2, 0);
    lv_obj_set_height(s_state.values[0], 18);
    lv_obj_clear_flag(s_state.pages[0], LV_OBJ_FLAG_SCROLLABLE);
    s_state.aod_switch = switch_row(s_state.pages[1], "AOD", 5);
    buddy_app_ui_settings_create_pack_selector(s_state.pages[2], callbacks->pack_selector_event);
    s_state.time_status = plain_label(s_state.pages[3]);
    s_state.language = lv_dropdown_create(s_state.pages[4]);
    lv_obj_set_size(s_state.language, LV_PCT(100), 44);
    lv_dropdown_set_options(s_state.language, "中文\nEnglish");
    lv_obj_add_event_cb(s_state.language, settings_value, LV_EVENT_VALUE_CHANGED, (void *)3);
    s_state.name_button = action_button(s_state.pages[4], "BLE device name", 4);
    /* 名称、编辑入口和保存状态属于同一张可点击卡片。 */
    lv_obj_set_height(s_state.name_button, 94);
    lv_obj_set_style_pad_all(s_state.name_button, 0, 0);
    lv_obj_t *name_caption = lv_obj_get_child(s_state.name_button, 0);
    lv_obj_set_align(name_caption, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(name_caption, 8, 8);
    lv_obj_set_width(name_caption, LV_PCT(90));
    lv_label_set_long_mode(name_caption, LV_LABEL_LONG_DOT);
    s_state.name_value = plain_label(s_state.name_button);
    lv_obj_set_pos(s_state.name_value, 8, 32);
    lv_obj_set_width(s_state.name_value, LV_PCT(90));
    lv_label_set_long_mode(s_state.name_value, LV_LABEL_LONG_DOT);
    s_state.name_status = plain_label(s_state.name_button);
    lv_obj_set_pos(s_state.name_status, 8, 58);
    lv_obj_set_size(s_state.name_status, LV_PCT(90), 30);
    s_state.reset_button = action_button(s_state.pages[4], "Restore defaults", 5);
    lv_obj_set_style_bg_opa(s_state.root_page, LV_OPA_TRANSP, 0);
    for (size_t i = 0; i < 6; ++i)
        lv_obj_set_style_bg_opa(s_state.pages[i], LV_OPA_TRANSP, 0);
    const char *diag[] = {"Portrait", "Landscape", "Diagnostics"};
    for (size_t i = 0; i < 3; ++i) s_state.diag_buttons[i] = action_button(s_state.pages[5], diag[i], i < 2 ? i + 6 : 9);
    s_state.maintenance = plain_label(s_state.pages[5]);
    /* 编辑覆盖层挂在屏幕上，横屏也能保留 44px 键盘和操作按钮。 */
    s_state.name_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_flex_flow(s_state.name_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_state.name_dialog, 2, 0);
    lv_obj_set_style_pad_row(s_state.name_dialog, 0, 0);
    s_state.name_title = plain_label(s_state.name_dialog); lv_obj_set_height(s_state.name_title, 18);
    s_state.name_ta = lv_textarea_create(s_state.name_dialog);
    lv_textarea_set_one_line(s_state.name_ta, true); lv_textarea_set_max_length(s_state.name_ta, BUDDY_APP_BLE_NAME_MAX - 1);
    lv_obj_set_size(s_state.name_ta, LV_PCT(100), 36);
    s_state.keyboard = lv_buttonmatrix_create(s_state.name_dialog);
    lv_obj_set_width(s_state.keyboard, LV_PCT(100));
    lv_obj_set_style_pad_all(s_state.keyboard, 0, 0);
    lv_obj_set_style_pad_row(s_state.keyboard, 0, 0); lv_obj_set_style_pad_column(s_state.keyboard, 0, 0);
    lv_obj_add_event_cb(s_state.keyboard, settings_keyboard, LV_EVENT_VALUE_CHANGED, NULL);
    s_state.name_save = action_button(s_state.name_dialog, "Save", 12);
    s_state.name_cancel = action_button(s_state.name_dialog, "Cancel", 13);
    /* 两个 44px 操作并排，避免横屏键盘挤掉确认和取消。 */
    lv_obj_t *actions = lv_obj_create(s_state.name_dialog); lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), 44); lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_parent(s_state.name_save, actions); lv_obj_set_parent(s_state.name_cancel, actions);
    lv_obj_set_width(s_state.name_save, LV_PCT(50)); lv_obj_set_width(s_state.name_cancel, LV_PCT(50));
    lv_obj_add_flag(s_state.name_dialog, LV_OBJ_FLAG_HIDDEN);
    /* 全屏遮罩接收框外触摸，防止确认期间触发背景设置或导航。 */
    s_state.reset_dialog = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_state.reset_dialog);
    lv_obj_add_flag(s_state.reset_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_state.reset_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_state.reset_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_state.reset_dialog, LV_OPA_50, 0);
    lv_obj_t *reset_card = lv_obj_create(s_state.reset_dialog);
    lv_obj_set_size(reset_card, LV_PCT(94), 180); lv_obj_center(reset_card);
    lv_obj_set_flex_flow(reset_card, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_all(reset_card, 4, 0);
    s_state.reset_body = plain_label(reset_card);
    s_state.reset_confirm = action_button(reset_card, "Restore", 10);
    s_state.reset_cancel = action_button(reset_card, "Cancel", 11);
    lv_obj_add_flag(s_state.reset_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_menu_set_page(s_state.menu, s_state.root_page);
    buddy_app_ui_settings_apply_locale();
    return true;
}
void buddy_app_ui_settings_layout(bool landscape, int32_t content_height, int32_t inset,
                                  int32_t top, int32_t card_width)
{
    if (s_state.page == NULL) return;
    lv_obj_set_pos(s_state.menu, inset, top);
    lv_obj_set_size(s_state.menu, card_width, content_height - top - 4);
    s_keyboard_landscape = landscape;
    lv_obj_set_pos(s_state.name_dialog, 0, 0);
    lv_obj_set_size(s_state.name_dialog, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
    lv_obj_set_size(s_state.reset_dialog, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
    lv_obj_center(lv_obj_get_child(s_state.reset_dialog, 0));
    lv_obj_set_height(s_state.keyboard, landscape ? 132 : 176);
    settings_keyboard_map_update();
}
static void font_tree(lv_obj_t *obj)
{
    lv_obj_set_style_text_font(obj, ui_locale_font(BUDDY_APP_FONT_BODY), 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) font_tree(lv_obj_get_child(obj, i));
}
void buddy_app_ui_settings_apply_locale(void)
{
    if (s_state.page == NULL) return;
    const char *en[] = {"Display", "AOD", "Character packs", "Time", "Device", "Diagnostics"};
    const char *zh[] = {"显示", "AOD", "角色包", "时间", "设备", "诊断"};
    lv_menu_set_page_title(s_state.root_page, ui_text(UI_TEXT_SETTINGS));
    for (size_t i = 0; i < 6; ++i) {
        const char *title = settings_text(en[i], zh[i]);
        button_text(s_state.root_buttons[i], title); lv_menu_set_page_title(s_state.pages[i], title);
    }
    lv_label_set_text(s_state.pack_caption_label, ui_text(UI_TEXT_CURRENT_PACK));
    button_text(s_state.name_button, ui_text(UI_TEXT_BLE_DEVICE_NAME));
    button_text(s_state.reset_button, settings_text("Restore defaults", "恢复默认设置"));
    const char *den[] = {"Portrait", "Landscape", "Diagnostics"};
    const char *dzh[] = {"竖屏", "横屏", "设备诊断"};
    for (size_t i = 0; i < 3; ++i) button_text(s_state.diag_buttons[i], settings_text(den[i], dzh[i]));
    lv_label_set_text(lv_obj_get_child(lv_obj_get_parent(s_state.dark_switch), 0), settings_text("Dark", "深色"));
    lv_label_set_text(s_state.name_title, ui_text(UI_TEXT_BLE_DEVICE_NAME));
    button_text(s_state.name_save, settings_text("Save", "保存")); button_text(s_state.name_cancel, settings_text("Cancel", "取消"));
    lv_label_set_text(s_state.reset_body, settings_text("Restore display settings and touch calibration? Packs and bonds are retained.", "恢复显示设置和触摸校准？角色包和配对保留。"));
    button_text(s_state.reset_confirm, settings_text("Restore", "恢复")); button_text(s_state.reset_cancel, settings_text("Cancel", "取消"));
    lv_label_set_text(s_state.maintenance, settings_text("Storage failure: use serial console storage format ERASE. Deletes packs and stored fonts.", "存储故障时通过串口执行 storage format ERASE，将删除角色包和存储字库。"));
    font_tree(s_state.menu); font_tree(s_state.name_dialog); font_tree(s_state.reset_dialog);
    settings_style_label(s_state.name_status, ui_locale_static_font(BUDDY_APP_FONT_META),
                         lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_obj_set_style_text_font(s_state.keyboard, BUDDY_APP_FONT_BODY, 0);
    /* 语言选项始终混排中英文；列表弹出后属于屏幕，不在 menu 对象树内。
     * 普通文字和选中行必须使用同一字体，保证点击行的高度与绘制一致。 */
    const lv_font_t *language_font = ui_locale_content_font();
    lv_obj_set_style_text_font(s_state.language, language_font, LV_PART_MAIN);
    lv_obj_t *list = lv_dropdown_get_list(s_state.language);
    lv_obj_set_style_text_font(list, language_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, language_font, LV_PART_SELECTED);
    /* 两个选项各保留至少 44px 行高，绘制与点击使用相同的行间距。 */
    int32_t line_space = language_font->line_height < 44 ? 44 - language_font->line_height : 0;
    lv_obj_set_style_text_line_space(list, line_space, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(list, line_space, LV_PART_SELECTED);
    /* 首尾各补半行间距，避免首项/末项的点击区被列表边缘裁掉。 */
    lv_obj_set_style_pad_top(list, (line_space + 1) / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list, (line_space + 1) / 2, LV_PART_MAIN);
    /* menu 的值变更事件只刷新标题；重新 set_page 会增加一次返回历史。 */
    lv_obj_send_event(s_state.menu, LV_EVENT_VALUE_CHANGED, NULL);
}
void buddy_app_ui_settings_refresh(const buddy_app_ui_snapshot_t *snapshot,
                                   bool interaction_blocked)
{
    if (snapshot == NULL || s_state.page == NULL) return;
    if (interaction_blocked) {
        buddy_app_ui_settings_close_name_dialog(); buddy_app_ui_settings_close_reset_dialog();
    }
    s_syncing = true;
    const int values[] = {snapshot->display_brightness_percent, snapshot->aod_timeout_minutes, snapshot->aod_brightness_percent};
    const char *en[] = {"Brightness", "AOD minutes", "AOD brightness"};
    const char *zh[] = {"屏幕亮度", "AOD 分钟", "AOD 亮度"};
    for (size_t i = 0; i < 3; ++i) {
        if (!lv_obj_has_state(s_state.sliders[i], LV_STATE_PRESSED)) lv_slider_set_value(s_state.sliders[i], values[i], LV_ANIM_OFF);
        char value[64]; snprintf(value, sizeof(value), "%s: %d%s", settings_text(en[i], zh[i]), values[i], i == 1 ? "" : "%");
        lv_label_set_text(s_state.values[i], value);
    }
    if (snapshot->aod_enabled) lv_obj_add_state(s_state.aod_switch, LV_STATE_CHECKED); else lv_obj_remove_state(s_state.aod_switch, LV_STATE_CHECKED);
    if (snapshot->theme >= 2) lv_obj_add_state(s_state.dark_switch, LV_STATE_CHECKED); else lv_obj_remove_state(s_state.dark_switch, LV_STATE_CHECKED);
    for (size_t i = 0; i < 2; ++i) {
        if (snapshot->theme % 2 == i) lv_obj_add_state(s_state.theme_buttons[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(s_state.theme_buttons[i], LV_STATE_CHECKED);
    }
    lv_dropdown_set_selected(s_state.language, ui_locale_get() == UI_LOCALE_ZH_CN ? 0 : 1);
    char text[192];
    lv_label_set_text(s_state.name_value, snapshot->ble_device_name);
    snprintf(text, sizeof(text), "%s",
             snapshot->device_names_pending ? settings_text("Saving...", "正在保存...") :
             snapshot->device_names_result == ESP_OK ? settings_text("Saved; takes effect after reboot", "已保存，重启后生效") :
             snapshot->device_names_result == ESP_ERR_INVALID_STATE ? settings_text("Reboot to apply changes", "修改后重启生效") : esp_err_to_name(snapshot->device_names_result));
    lv_label_set_text(s_state.name_status, text);
    if (snapshot->time_source == BUDDY_APP_TIME_UNSYNCED) {
        lv_label_set_text(s_state.time_status, settings_text("Not synchronized. Connect the computer plugin over secure BLE.", "未校时。请通过安全蓝牙连接电脑插件。"));
    } else {
        time_t last = snapshot->time_last_synced_seconds + snapshot->tz_offset_seconds;
        struct tm tm = {0}; gmtime_r(&last, &tm);
        char date[32]; strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
        int32_t offset = snapshot->tz_offset_seconds;
        snprintf(text, sizeof(text), "%s\n%s\nUTC%c%02ld:%02ld", settings_text("Last computer sync", "最近电脑校时"), date,
                 offset < 0 ? '-' : '+', (long)(abs(offset) / 3600), (long)((abs(offset) / 60) % 60));
        lv_label_set_text(s_state.time_status, text);
    }
    s_syncing = false;
}

bool buddy_app_ui_settings_create_pack_selector(lv_obj_t *parent, lv_event_cb_t event_cb)
{
    if (s_state.pack_selector != NULL) return true;
    if (parent == NULL) return false;
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(BUDDY_APP_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, BUDDY_APP_UI_CARD_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(BUDDY_APP_COLOR_ACCENT), 0);
    lv_obj_set_style_outline_opa(card, LV_OPA_60, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    /* 名称独占整行并自动换行，卡片随文本增高；状态另起一行，避免挤掉名称。 */
    s_state.pack_caption_label = lv_label_create(card);
    lv_obj_set_width(s_state.pack_caption_label, LV_PCT(100));
    settings_style_label(s_state.pack_caption_label, BUDDY_APP_FONT_META, lv_color_hex(BUDDY_APP_COLOR_MUTED));
    s_state.pack_detail_label = lv_label_create(card);
    lv_obj_set_size(s_state.pack_detail_label, LV_PCT(100), LV_SIZE_CONTENT);
    settings_style_label(s_state.pack_detail_label, BUDDY_APP_FONT_BODY, lv_color_hex(BUDDY_APP_COLOR_TEXT));
    lv_label_set_long_mode(s_state.pack_detail_label, LV_LABEL_LONG_WRAP);
    s_state.pack_status_label = lv_label_create(card);
    lv_obj_set_size(s_state.pack_status_label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_state.pack_status_label, LV_TEXT_ALIGN_RIGHT, 0);
    settings_style_label(s_state.pack_status_label, BUDDY_APP_FONT_META, lv_color_hex(BUDDY_APP_COLOR_MUTED));
    s_state.pack_selector = lv_dropdown_create(parent); lv_obj_set_size(s_state.pack_selector, LV_PCT(100), 44); lv_dropdown_set_options(s_state.pack_selector, ui_text(UI_TEXT_NO_PACK)); if (event_cb != NULL) lv_obj_add_event_cb(s_state.pack_selector, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return s_state.pack_caption_label != NULL && s_state.pack_detail_label != NULL && s_state.pack_status_label != NULL && s_state.pack_selector != NULL;
}
lv_obj_t *buddy_app_ui_settings_pack_caption_label(void) { return s_state.pack_caption_label; }
bool buddy_app_ui_settings_pack_selector_exists(void) { return s_state.pack_selector != NULL; }
uint32_t buddy_app_ui_settings_pack_selector_selected(void) { return s_state.pack_selector == NULL ? 0 : lv_dropdown_get_selected(s_state.pack_selector); }
void buddy_app_ui_settings_invalidate_pack_selector(void) { if (s_state.pack_selector != NULL) lv_obj_invalidate(s_state.pack_selector); }
void buddy_app_ui_settings_set_pack_detail(const char *text) { if (s_state.pack_detail_label != NULL) lv_label_set_text(s_state.pack_detail_label, text != NULL ? text : ""); }
static const char *pack_text(const char *en, const char *zh) { return ui_locale_get() == UI_LOCALE_ZH_CN ? zh : en; }
void buddy_app_ui_settings_update_pack_selector(const buddy_app_ui_snapshot_t *snapshot, bool switch_request_failed)
{
    char options[BUDDY_APP_CHARPACK_LIST_MAX * (EXAMPLE_CHARPACK_PACK_ID_MAX + 2)] = {0}; size_t count, selected = 0; bool list_ready, switch_failed;
    if (s_state.pack_selector == NULL || snapshot == NULL) return;
    count = snapshot->installed_pack_count; if (count > BUDDY_APP_CHARPACK_LIST_MAX) count = BUDDY_APP_CHARPACK_LIST_MAX; list_ready = !snapshot->charpack_list_pending && snapshot->charpack_list_result == ESP_OK; switch_failed = !snapshot->charpack_switch_pending && (snapshot->charpack_switch_result != ESP_OK || switch_request_failed);
    if (snapshot->charpack_list_pending) strlcpy(options, pack_text("Refreshing packs...", "正在刷新角色包..."), sizeof(options)); else if (!list_ready) strlcpy(options, pack_text("Retry pack list", "重试角色包列表"), sizeof(options)); else if (count == 0) strlcpy(options, pack_text("No installed packs", "没有已安装角色包"), sizeof(options)); else for (size_t i = 0; i < count; ++i) { if (i != 0) strlcat(options, "\n", sizeof(options)); strlcat(options, snapshot->installed_packs[i].pack_id, sizeof(options)); if (snapshot->have_active_pack && strcmp(snapshot->installed_packs[i].pack_id, snapshot->active_pack.pack_id) == 0) selected = i; }
    lv_dropdown_set_options(s_state.pack_selector, options); lv_dropdown_set_selected(s_state.pack_selector, (uint32_t)selected);
    if (snapshot->charpack_list_pending || (list_ready && count == 0) || snapshot->charpack_switch_pending) lv_obj_add_state(s_state.pack_selector, LV_STATE_DISABLED); else lv_obj_remove_state(s_state.pack_selector, LV_STATE_DISABLED);
    const char *status;
    uint32_t color = BUDDY_APP_COLOR_MUTED;
    /* 切换成功后会异步刷新列表；列表未就绪不能当作切换失败。
     * 真正的切换错误优先于列表状态，避免刷新掩盖执行或排队失败。 */
    if (snapshot->charpack_switch_pending) {
        status = ui_text(UI_TEXT_SWITCHING_PACK);
        color = BUDDY_APP_COLOR_ACCENT;
    } else if (switch_failed) {
        status = ui_text(UI_TEXT_SWITCH_FAILED);
        color = BUDDY_APP_COLOR_DENY;
    } else if (snapshot->charpack_list_pending) {
        status = pack_text("Refreshing...", "正在刷新...");
        color = BUDDY_APP_COLOR_ACCENT;
    } else if (!list_ready) {
        status = pack_text("List failed", "列表失败");
        color = BUDDY_APP_COLOR_DENY;
    } else {
        status = ui_text(snapshot->have_active_pack ? UI_TEXT_READY : UI_TEXT_NO_PACK);
    }
    lv_label_set_text(s_state.pack_status_label, status);
    lv_obj_set_style_text_color(s_state.pack_status_label, lv_color_hex(color), 0);
}
