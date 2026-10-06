#include "ui_theme.h"

/* UI 任务内的单一调色板。角色图片和 AOD 的暗底不参与重着色。 */
static const uint32_t s_palette[BUDDY_APP_THEME_COUNT][UI_COLOR_COUNT] = {
    [BUDDY_APP_THEME_DEEPSEEK] = {
        [UI_COLOR_BG] = 0xE9EEF8, [UI_COLOR_PANEL] = 0xFAFBFF,
        [UI_COLOR_PANEL_ALT] = 0xD8DFEC, [UI_COLOR_TEXT] = 0x0F1115,
        [UI_COLOR_MUTED] = 0x59616E, [UI_COLOR_ACCENT] = 0x4D6BFE,
        [UI_COLOR_ACCENT_SOFT] = 0xEAF0FE, [UI_COLOR_ALLOW] = 0x425FE8,
        [UI_COLOR_ALLOW_BG] = 0xE8EEFF, [UI_COLOR_PASSKEY] = 0x3B56D9,
        [UI_COLOR_SWITCH_KNOB] = 0xF4F6FC,
        [UI_COLOR_INFO_BG] = 0xF1F4FB, [UI_COLOR_SEPARATOR] = 0x9DAAC0,
        [UI_COLOR_DENY] = 0xB44136, [UI_COLOR_DENY_BG] = 0xFBEDEA,
    },
    [BUDDY_APP_THEME_CLAUDE] = {
        [UI_COLOR_BG] = 0xECE8DF, [UI_COLOR_PANEL] = 0xFCFCFB,
        [UI_COLOR_PANEL_ALT] = 0xD9D4C9, [UI_COLOR_TEXT] = 0x141413,
        [UI_COLOR_MUTED] = 0x625F56, [UI_COLOR_ACCENT] = 0xB85E40,
        [UI_COLOR_ACCENT_SOFT] = 0xF5E7DF, [UI_COLOR_ALLOW] = 0x247A55,
        [UI_COLOR_ALLOW_BG] = 0xE5F2EA, [UI_COLOR_PASSKEY] = 0xA94F2F,
        [UI_COLOR_SWITCH_KNOB] = 0xF4F6FC,
        [UI_COLOR_INFO_BG] = 0xF4F1EA, [UI_COLOR_SEPARATOR] = 0xA99F8D,
        [UI_COLOR_DENY] = 0xB44136, [UI_COLOR_DENY_BG] = 0xFBEDEA,
    },
    [BUDDY_APP_THEME_DEEPSEEK_DARK] = {
        [UI_COLOR_BG] = 0x13161D, [UI_COLOR_PANEL] = 0x212631,
        [UI_COLOR_PANEL_ALT] = 0x3C4759, [UI_COLOR_TEXT] = 0xEEF1F8,
        [UI_COLOR_MUTED] = 0xAEB5C4, [UI_COLOR_ACCENT] = 0x6A84FF,
        [UI_COLOR_ACCENT_SOFT] = 0x232A3A, [UI_COLOR_ALLOW] = 0x8197FF,
        [UI_COLOR_ALLOW_BG] = 0x26314A, [UI_COLOR_PASSKEY] = 0x8FA4FF,
        [UI_COLOR_SWITCH_KNOB] = 0xF4F6FC,
        [UI_COLOR_INFO_BG] = 0x1B202A, [UI_COLOR_SEPARATOR] = 0x59677B,
        [UI_COLOR_DENY] = 0xFF8A7F, [UI_COLOR_DENY_BG] = 0x3B2527,
    },
    [BUDDY_APP_THEME_CLAUDE_DARK] = {
        [UI_COLOR_BG] = 0x141413, [UI_COLOR_PANEL] = 0x242320,
        [UI_COLOR_PANEL_ALT] = 0x46423C, [UI_COLOR_TEXT] = 0xFAF9F5,
        [UI_COLOR_MUTED] = 0xC2BEB4, [UI_COLOR_ACCENT] = 0xE19376,
        [UI_COLOR_ACCENT_SOFT] = 0x3B2C25, [UI_COLOR_ALLOW] = 0x79CEA1,
        [UI_COLOR_ALLOW_BG] = 0x203A2D, [UI_COLOR_PASSKEY] = 0xE08A6D,
        [UI_COLOR_SWITCH_KNOB] = 0xF4F6FC,
        [UI_COLOR_INFO_BG] = 0x1E1D1A, [UI_COLOR_SEPARATOR] = 0x686158,
        [UI_COLOR_DENY] = 0xFF8A7F, [UI_COLOR_DENY_BG] = 0x3B2527,
    },
};
static buddy_app_theme_t s_theme = BUDDY_APP_THEME_DEEPSEEK;
static lv_theme_t *s_lv_theme;

uint32_t buddy_ui_theme_color(buddy_ui_color_t role)
{
    return role < UI_COLOR_COUNT ? s_palette[s_theme][role] : s_palette[s_theme][UI_COLOR_TEXT];
}

static void theme_widget(lv_theme_t *theme, lv_obj_t *obj)
{
    (void)theme;
    lv_obj_set_style_text_color(obj, lv_color_hex(BUDDY_APP_COLOR_TEXT), 0);
    lv_obj_set_style_outline_color(obj, lv_color_hex(BUDDY_APP_COLOR_TEXT), 0);
    /* 默认控件也使用中性面板；标签不增加背景，交互尺寸由页面控制。 */
    if (!lv_obj_check_type(obj, &lv_label_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(BUDDY_APP_COLOR_PANEL), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
    }
    if (lv_obj_check_type(obj, &lv_textarea_class) || lv_obj_check_type(obj, &lv_dropdown_class) ||
        lv_obj_check_type(obj, &lv_roller_class) || lv_obj_check_type(obj, &lv_spinbox_class))
        lv_obj_set_style_border_width(obj, BUDDY_APP_UI_BORDER_WIDTH, 0);
    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_set_style_radius(obj, 8, 0);
        lv_obj_set_style_border_width(obj, BUDDY_APP_UI_BORDER_WIDTH, 0);
    }
    if (lv_obj_check_type(obj, &lv_switch_class)) {
        /* 开关轨道与圆形滑块有独立轮廓，不沿用卡片边框。 */
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(BUDDY_APP_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(buddy_ui_theme_color(UI_COLOR_SWITCH_KNOB)), LV_PART_KNOB);
    }
    if (lv_obj_check_type(obj, &lv_buttonmatrix_class)) {
        lv_obj_set_style_border_width(obj, BUDDY_APP_UI_BORDER_WIDTH, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(obj, lv_color_hex(BUDDY_APP_COLOR_PANEL), LV_PART_ITEMS);
        lv_obj_set_style_text_color(obj, lv_color_hex(BUDDY_APP_COLOR_TEXT), LV_PART_ITEMS);
        lv_obj_set_style_border_color(obj, lv_color_hex(BUDDY_APP_COLOR_PANEL_ALT), LV_PART_ITEMS);
    }
}

esp_err_t buddy_ui_theme_init(lv_display_t *display, buddy_app_theme_t theme)
{
    s_theme = theme < BUDDY_APP_THEME_COUNT ? theme : BUDDY_APP_THEME_DEEPSEEK;
    lv_theme_t *base = lv_theme_default_init(display, lv_color_hex(BUDDY_APP_COLOR_ACCENT),
                                            lv_color_hex(BUDDY_APP_COLOR_DENY), s_theme >= BUDDY_APP_THEME_DEEPSEEK_DARK, LV_FONT_DEFAULT);
    /* 与显示器同生命周期，只创建一次，不在切换时重建页面或回调。 */
    if (s_lv_theme == NULL) s_lv_theme = lv_theme_create();
    if (s_lv_theme == NULL) return ESP_ERR_NO_MEM;
    {
        lv_theme_copy(s_lv_theme, base);
        lv_theme_set_parent(s_lv_theme, base);
        lv_theme_set_apply_cb(s_lv_theme, theme_widget);
        lv_display_set_theme(display, s_lv_theme);
    }
    return ESP_OK;
}

static lv_color_t recolor(lv_color_t color, buddy_app_theme_t previous)
{
    for (size_t i = 0; i < UI_COLOR_COUNT; ++i) {
        if (lv_color_eq(color, lv_color_hex(s_palette[previous][i])))
            return lv_color_hex(s_palette[s_theme][i]);
    }
    return color;
}

static void theme_tree(lv_obj_t *obj, buddy_app_theme_t previous)
{
    static const lv_style_prop_t props[] = {LV_STYLE_BG_COLOR, LV_STYLE_TEXT_COLOR,
        LV_STYLE_BORDER_COLOR, LV_STYLE_OUTLINE_COLOR, LV_STYLE_LINE_COLOR,
        LV_STYLE_ARC_COLOR, LV_STYLE_SHADOW_COLOR};
    static const lv_part_t parts[] = {LV_PART_MAIN, LV_PART_SCROLLBAR, LV_PART_INDICATOR,
        LV_PART_KNOB, LV_PART_SELECTED, LV_PART_ITEMS, LV_PART_CURSOR};
    static const lv_state_t states[] = {0, LV_STATE_PRESSED, LV_STATE_CHECKED, LV_STATE_DISABLED};
    /* 只更新现有局部颜色，不改布局、隐藏标记、滚动位置或输入内容。
     * 页面新增颜色状态时应在此补充 selector；默认主题的共享样式由 IDF LVGL 更新。 */
    for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); ++s) {
            lv_style_selector_t selector = parts[p] | states[s];
            for (size_t c = 0; c < sizeof(props) / sizeof(props[0]); ++c) {
                lv_style_value_t value;
                if (lv_obj_get_local_style_prop(obj, props[c], &value, selector) == LV_STYLE_RES_FOUND) {
                    lv_color_t next = recolor(value.color, previous);
                    if (!lv_color_eq(value.color, next)) {
                        value.color = next;
                        lv_obj_set_local_style_prop(obj, props[c], value, selector);
                    }
                }
            }
        }
    }
    if (lv_obj_check_type(obj, &lv_chart_class)) {
        lv_chart_series_t *series = NULL;
        while ((series = lv_chart_get_series_next(obj, series)) != NULL)
            lv_chart_set_series_color(obj, series, recolor(lv_chart_get_series_color(obj, series), previous));
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) theme_tree(lv_obj_get_child(obj, i), previous);
}

void buddy_ui_theme_apply(lv_display_t *display, buddy_app_theme_t theme)
{
    if (theme >= BUDDY_APP_THEME_COUNT || theme == s_theme) return;
    buddy_app_theme_t previous = s_theme;
    /* 首次初始化已确认主题对象存在，切换不再分配主题对象。 */
    (void)buddy_ui_theme_init(display, theme);
    theme_tree(lv_display_get_screen_active(display), previous);
    theme_tree(lv_display_get_layer_top(display), previous);
    theme_tree(lv_display_get_layer_sys(display), previous);
    /* 默认主题被包装后，LVGL 不会自动刷新已有控件的共享样式。 */
    lv_obj_report_style_change(NULL);
}
