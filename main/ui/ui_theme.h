#pragma once

#include "app_shared.h"
#include "lvgl.h"

#define BUDDY_APP_UI_BORDER_WIDTH 2
/* 信息容器弱轮廓；动作和输入使用清晰的 2px 边界。 */
#define BUDDY_APP_UI_CARD_BORDER_WIDTH 1

/* 调色板仅由持有 adapter lock 的 UI 任务读取和切换。 */
typedef enum {
    UI_COLOR_PANEL,
    UI_COLOR_PANEL_ALT,
    UI_COLOR_TEXT,
    UI_COLOR_MUTED,
    UI_COLOR_ACCENT,
    UI_COLOR_DENY,
    UI_COLOR_DENY_BG,
    UI_COLOR_ACCENT_SOFT,
    UI_COLOR_BG,
    UI_COLOR_ALLOW,
    UI_COLOR_PASSKEY,
    UI_COLOR_ALLOW_BG,
    UI_COLOR_SWITCH_KNOB,
    UI_COLOR_INFO_BG,
    UI_COLOR_SEPARATOR,
    UI_COLOR_COUNT
} buddy_ui_color_t;

uint32_t buddy_ui_theme_color(buddy_ui_color_t role);
esp_err_t buddy_ui_theme_init(lv_display_t *display, buddy_app_theme_t theme);
void buddy_ui_theme_apply(lv_display_t *display, buddy_app_theme_t theme);

#define BUDDY_APP_UI_COLOR_PANEL buddy_ui_theme_color(UI_COLOR_PANEL)
#define BUDDY_APP_UI_COLOR_PANEL_ALT buddy_ui_theme_color(UI_COLOR_PANEL_ALT)
#define BUDDY_APP_UI_COLOR_TEXT buddy_ui_theme_color(UI_COLOR_TEXT)
#define BUDDY_APP_UI_COLOR_MUTED buddy_ui_theme_color(UI_COLOR_MUTED)
#define BUDDY_APP_UI_COLOR_ACCENT buddy_ui_theme_color(UI_COLOR_ACCENT)
#define BUDDY_APP_UI_COLOR_DENY buddy_ui_theme_color(UI_COLOR_DENY)
#define BUDDY_APP_COLOR_TEXT buddy_ui_theme_color(UI_COLOR_TEXT)
#define BUDDY_APP_COLOR_ACCENT buddy_ui_theme_color(UI_COLOR_ACCENT)
#define BUDDY_APP_COLOR_DENY buddy_ui_theme_color(UI_COLOR_DENY)
#define BUDDY_APP_COLOR_DENY_BG buddy_ui_theme_color(UI_COLOR_DENY_BG)
#define BUDDY_APP_COLOR_PANEL buddy_ui_theme_color(UI_COLOR_PANEL)
#define BUDDY_APP_COLOR_PANEL_ALT buddy_ui_theme_color(UI_COLOR_PANEL_ALT)
#define BUDDY_APP_COLOR_MUTED buddy_ui_theme_color(UI_COLOR_MUTED)
#define BUDDY_APP_COLOR_ACCENT_SOFT buddy_ui_theme_color(UI_COLOR_ACCENT_SOFT)
#define BUDDY_APP_COLOR_BG buddy_ui_theme_color(UI_COLOR_BG)
#define BUDDY_APP_COLOR_ALLOW buddy_ui_theme_color(UI_COLOR_ALLOW)
#define BUDDY_APP_COLOR_PASSKEY buddy_ui_theme_color(UI_COLOR_PASSKEY)
#define BUDDY_APP_COLOR_ALLOW_BG buddy_ui_theme_color(UI_COLOR_ALLOW_BG)

#define BUDDY_APP_COLOR_INFO_BG buddy_ui_theme_color(UI_COLOR_INFO_BG)

#define BUDDY_APP_COLOR_SEPARATOR buddy_ui_theme_color(UI_COLOR_SEPARATOR)
