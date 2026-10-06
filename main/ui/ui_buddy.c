/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_theme.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "src/widgets/gif/lv_gif.h"

#include "ui_buddy.h"
#include "ui_locale.h"

#define BUDDY_APP_GIF_PATH_MAX 224
#define BUDDY_APP_GIF_CARD_INSET 4
#define BUDDY_APP_GIF_MAX_SCALE (LV_SCALE_NONE * 2U)

#if CONFIG_LV_FONT_MONTSERRAT_14
#define BUDDY_APP_FONT_BODY (&lv_font_montserrat_14)
#else
#define BUDDY_APP_FONT_BODY LV_FONT_DEFAULT
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

static struct {
    lv_obj_t *page, *title_label, *status_label, *detail_card;
    lv_obj_t *metric_labels[5];
    lv_obj_t *metric_titles[3];
    lv_obj_t *gif_card, *gif_obj, *gif_label;
    bool gif_running;
    char gif_pack_id[EXAMPLE_CHARPACK_PACK_ID_MAX + 1];
    char gif_src[BUDDY_APP_GIF_PATH_MAX];
    uint32_t gif_source_width, gif_source_height;
    buddy_app_ui_buddy_gif_progress_t gif_progress;
} s_buddy = { .gif_progress.frame_index = -1, .gif_progress.frame_count = -1 };

static void style_label(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static void style_card(lv_obj_t *obj, uint32_t bg_color, uint32_t border_color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, BUDDY_APP_UI_CARD_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border_color), 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_outline_color(obj, lv_color_hex(BUDDY_APP_COLOR_ACCENT), 0);
    lv_obj_set_style_outline_opa(obj, LV_OPA_60, 0);
}

static lv_obj_t *create_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static lv_obj_t *create_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y); lv_obj_set_size(card, width, height);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    style_card(card, BUDDY_APP_COLOR_INFO_BG, BUDDY_APP_COLOR_PANEL_ALT);
    return card;
}

bool buddy_app_ui_buddy_create(void *context)
{
    lv_obj_t *parent = context;
    if (s_buddy.page != NULL) return true;
    if (parent == NULL) return false;
    s_buddy.page = create_page(parent);
    if (s_buddy.page == NULL) return false;
    s_buddy.title_label = lv_label_create(s_buddy.page);
    style_label(s_buddy.title_label, BUDDY_APP_FONT_TITLE, lv_color_hex(BUDDY_APP_COLOR_ACCENT));
    lv_label_set_long_mode(s_buddy.title_label, LV_LABEL_LONG_DOT); lv_label_set_text(s_buddy.title_label, ui_text(UI_TEXT_BUDDY));
    s_buddy.status_label = lv_label_create(s_buddy.page);
    style_label(s_buddy.status_label, BUDDY_APP_FONT_META, lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_obj_set_style_text_align(s_buddy.status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_buddy.status_label, LV_LABEL_LONG_DOT); lv_label_set_text(s_buddy.status_label, ui_text(UI_TEXT_DISCONNECTED));
    s_buddy.gif_card = lv_obj_create(s_buddy.page);
    style_card(s_buddy.gif_card, BUDDY_APP_COLOR_INFO_BG, BUDDY_APP_COLOR_PANEL_ALT);
    s_buddy.gif_obj = lv_gif_create(s_buddy.gif_card);
    /* 保留 GIF 透明索引的 alpha，让角色卡底色随主题透出；不对角色原色着色。
     * ARGB8888 画布位于 LVGL 的 PSRAM 堆，LCD 仍使用 RGB565 DMA 缓冲。 */
    lv_gif_set_color_format(s_buddy.gif_obj, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_style_bg_opa(s_buddy.gif_obj, LV_OPA_TRANSP, 0);
    /* 放大透明角色时插值轮廓 alpha，避免浅色卡片上出现阶梯边缘。 */
    lv_image_set_antialias(s_buddy.gif_obj, true);
    lv_obj_center(s_buddy.gif_obj); lv_obj_add_flag(s_buddy.gif_obj, LV_OBJ_FLAG_HIDDEN);
    s_buddy.gif_label = lv_label_create(s_buddy.gif_card);
    lv_obj_center(s_buddy.gif_label); lv_label_set_long_mode(s_buddy.gif_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_buddy.gif_label, LV_TEXT_ALIGN_CENTER, 0);
    style_label(s_buddy.gif_label, BUDDY_APP_FONT_META, lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_label_set_text(s_buddy.gif_label, ui_text(UI_TEXT_NO_PACK));
    s_buddy.detail_card = create_card(s_buddy.page, 0, 0, 44, 44);
    for (size_t i = 0; i < 5; ++i) {
        s_buddy.metric_labels[i] = lv_label_create(s_buddy.detail_card);
        style_label(s_buddy.metric_labels[i], BUDDY_APP_FONT_META, lv_color_hex(BUDDY_APP_COLOR_MUTED));
        lv_label_set_long_mode(s_buddy.metric_labels[i], LV_LABEL_LONG_DOT); lv_label_set_text(s_buddy.metric_labels[i], "--");
    }
    /* 名称保留元信息字号，计数独立使用正文大小，避免字号被整行限制。 */
    for (size_t i = 0; i < 3; ++i) {
        s_buddy.metric_titles[i] = lv_label_create(s_buddy.detail_card);
        style_label(s_buddy.metric_titles[i], BUDDY_APP_FONT_META, lv_color_hex(BUDDY_APP_COLOR_MUTED));
        lv_label_set_long_mode(s_buddy.metric_titles[i], LV_LABEL_LONG_DOT);
        style_label(s_buddy.metric_labels[i], BUDDY_APP_FONT_BODY, lv_color_hex(BUDDY_APP_COLOR_TEXT));
        lv_obj_set_style_text_align(s_buddy.metric_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    return s_buddy.title_label != NULL && s_buddy.status_label != NULL && s_buddy.gif_card != NULL &&
           s_buddy.gif_obj != NULL && s_buddy.gif_label != NULL && s_buddy.detail_card != NULL && s_buddy.metric_labels[4] != NULL;
}

lv_obj_t *buddy_app_ui_buddy_root(void) { return s_buddy.page; }
void **buddy_app_ui_buddy_root_slot(void) { return (void **)&s_buddy.page; }

static void fit_gif_to_card(void)
{
    const int32_t card_width = s_buddy.gif_card != NULL ? lv_obj_get_width(s_buddy.gif_card) : 0;
    const int32_t card_height = s_buddy.gif_card != NULL ? lv_obj_get_height(s_buddy.gif_card) : 0;
    const int32_t available_width = card_width - BUDDY_APP_GIF_CARD_INSET * 2;
    const int32_t available_height = card_height - BUDDY_APP_GIF_CARD_INSET * 2;
    uint64_t width_scale, height_scale, scale;
    if (s_buddy.gif_obj == NULL || s_buddy.gif_source_width == 0 || s_buddy.gif_source_height == 0 || available_width <= 0 || available_height <= 0) return;
    width_scale = (uint64_t)(uint32_t)available_width * LV_SCALE_NONE / s_buddy.gif_source_width;
    height_scale = (uint64_t)(uint32_t)available_height * LV_SCALE_NONE / s_buddy.gif_source_height;
    scale = width_scale < height_scale ? width_scale : height_scale;
    if (scale > BUDDY_APP_GIF_MAX_SCALE) scale = BUDDY_APP_GIF_MAX_SCALE;
    if (scale == 0) scale = 1;
    lv_image_set_scale(s_buddy.gif_obj, (uint32_t)scale); lv_obj_center(s_buddy.gif_obj);
}

void buddy_app_ui_buddy_layout(bool landscape, int32_t content_height, int32_t inset, int32_t top, int32_t card_width)
{
    if (s_buddy.page == NULL) return;
    lv_obj_set_pos(s_buddy.title_label, inset, top); lv_obj_set_width(s_buddy.title_label, card_width / 2);
    lv_obj_set_pos(s_buddy.status_label, inset + card_width / 2, top + 3); lv_obj_set_size(s_buddy.status_label, card_width / 2, 18);
    if (landscape) {
        lv_obj_set_pos(s_buddy.gif_card, inset, top + 26); lv_obj_set_size(s_buddy.gif_card, 144, content_height - top - 38);
        lv_obj_set_pos(s_buddy.detail_card, inset + 152, top + 26); lv_obj_set_size(s_buddy.detail_card, card_width - 152, content_height - top - 38);
        for (size_t i = 0; i < 5; ++i) { lv_obj_set_pos(s_buddy.metric_labels[i], 7, 12 + (int32_t)i * 28); lv_obj_set_size(s_buddy.metric_labels[i], LV_PCT(84), 20); }
        lv_obj_set_style_text_letter_space(s_buddy.metric_labels[4], 0, 0);
    } else {
        lv_obj_set_pos(s_buddy.gif_card, inset, top + 26); lv_obj_set_size(s_buddy.gif_card, card_width, 156);
        lv_obj_set_pos(s_buddy.detail_card, inset, top + 188); lv_obj_set_size(s_buddy.detail_card, card_width, 44);
        for (size_t i = 0; i < 3; ++i) { int32_t width = (card_width - 16) / 3; lv_obj_set_pos(s_buddy.metric_labels[i], 8 + (int32_t)i * width, 4); lv_obj_set_size(s_buddy.metric_labels[i], width - 2, 18); }
        /* 为较长的 Token 数值留出宽度，同时保持上下文在卡片内。 */
        const int32_t token_width = 86;
        const int32_t context_x = 8 + token_width + 4;
        lv_obj_set_pos(s_buddy.metric_labels[3], 8, 26); lv_obj_set_size(s_buddy.metric_labels[3], token_width, 16);
        lv_obj_set_pos(s_buddy.metric_labels[4], context_x, 26); lv_obj_set_size(s_buddy.metric_labels[4], card_width - context_x - 8, 16);
        lv_obj_set_style_text_letter_space(s_buddy.metric_labels[4], -1, 0);
    }
    for (size_t i = 0; i < 3; ++i) {
        int32_t x = landscape ? 7 : 8 + (int32_t)i * ((card_width - 16) / 3);
        int32_t y = landscape ? 12 + (int32_t)i * 28 : 4;
        int32_t width = landscape ? card_width - 168 : (card_width - 16) / 3 - 2;
        lv_obj_set_pos(s_buddy.metric_titles[i], x, y + 1);
        lv_obj_set_size(s_buddy.metric_titles[i], width / 2, 17);
        lv_obj_set_pos(s_buddy.metric_labels[i], x + width / 2, y);
        lv_obj_set_size(s_buddy.metric_labels[i], width - width / 2, 18);
    }
    fit_gif_to_card(); lv_obj_set_size(s_buddy.gif_label, LV_PCT(88), LV_PCT(70)); lv_obj_center(s_buddy.gif_label);
}

void buddy_app_ui_buddy_apply_locale_fonts(void)
{
    if (s_buddy.page == NULL) return;
    const lv_font_t *title = ui_locale_font(BUDDY_APP_FONT_TITLE), *meta = ui_locale_font(BUDDY_APP_FONT_META);
    lv_obj_set_style_text_font(s_buddy.title_label, title, 0); lv_obj_set_style_text_font(s_buddy.status_label, meta, 0); lv_obj_set_style_text_font(s_buddy.gif_label, meta, 0);
    for (size_t i = 0; i < 5; ++i) lv_obj_set_style_text_font(s_buddy.metric_labels[i], i < 3 ? ui_locale_font(BUDDY_APP_FONT_BODY) : meta, 0);
    for (size_t i = 0; i < 3; ++i) lv_obj_set_style_text_font(s_buddy.metric_titles[i], meta, 0);
}

void buddy_app_ui_buddy_refresh(const example_buddy_state_cache_t *state,
                                const char *status_text,
                                lv_color_t status_color,
                                const char *tokens_text,
                                const char *context_text,
                                const char *context_compact_text)
{
    const char *total_text = "--", *running_text = "--", *waiting_text = "--"; char total_value[16], running_value[16], waiting_value[16];
    if (state == NULL || s_buddy.page == NULL) return;
    lv_label_set_text(s_buddy.status_label, status_text != NULL ? status_text : ""); lv_obj_set_style_text_color(s_buddy.status_label, status_color, 0);
    if (state->has_state) { snprintf(total_value, sizeof(total_value), "%lu", (unsigned long)state->total); snprintf(running_value, sizeof(running_value), "%lu", (unsigned long)state->running); snprintf(waiting_value, sizeof(waiting_value), "%lu", (unsigned long)state->waiting); total_text = total_value; running_text = running_value; waiting_text = waiting_value; }
    lv_label_set_text(s_buddy.metric_titles[0], ui_text(UI_TEXT_TOTAL));
    lv_label_set_text(s_buddy.metric_titles[1], ui_locale_get() == UI_LOCALE_EN_US ? "Run" : ui_text(UI_TEXT_RUNNING));
    lv_label_set_text(s_buddy.metric_titles[2], ui_locale_get() == UI_LOCALE_EN_US ? "Wait" : ui_text(UI_TEXT_WAITING));
    lv_label_set_text(s_buddy.metric_labels[0], total_text);
    lv_label_set_text(s_buddy.metric_labels[1], running_text);
    lv_label_set_text(s_buddy.metric_labels[2], waiting_text);
    lv_label_set_text_fmt(s_buddy.metric_labels[3], "%s  %s", ui_text(UI_TEXT_TOKEN_TOTAL), state->has_state && tokens_text != NULL ? tokens_text : "--");
    lv_label_set_text_fmt(s_buddy.metric_labels[4], "%s %s", ui_text(UI_TEXT_CONTEXT),
                          state->has_state && context_text != NULL ? context_text : "--");
    if ((lv_obj_get_width(s_buddy.detail_card) < 160 ||
         lv_obj_get_height(s_buddy.detail_card) <= 44) && context_compact_text != NULL) {
        lv_label_set_text_fmt(s_buddy.metric_labels[4], "%s %s", ui_text(UI_TEXT_CONTEXT),
                              state->has_state ? context_compact_text : "--");
    }
}

static bool path_exists(const char *path) { struct stat st; return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode); }
static bool build_gif_src(const char *pack_id, const char *desired_asset, char *out_src, size_t out_src_size)
{
    char candidate[BUDDY_APP_GIF_PATH_MAX], pack_root[BUDDY_APP_GIF_PATH_MAX]; const char *asset_name = desired_asset, *mount_point = CONFIG_EXAMPLE_CHARPACK_MOUNT_POINT, *relative_root = CONFIG_EXAMPLE_CHARPACK_PACKS_ROOT; size_t mount_len;
    if (pack_id == NULL || pack_id[0] == '\0' || desired_asset == NULL || out_src == NULL || out_src_size == 0) return false;
    if (snprintf(pack_root, sizeof(pack_root), "%s/%s", relative_root, pack_id) >= (int)sizeof(pack_root)) return false;
    if (snprintf(candidate, sizeof(candidate), "%s/%s", pack_root, desired_asset) >= (int)sizeof(candidate) || !path_exists(candidate)) { asset_name = "idle.gif"; if (strcmp(desired_asset, asset_name) == 0 || snprintf(candidate, sizeof(candidate), "%s/%s", pack_root, asset_name) >= (int)sizeof(candidate) || !path_exists(candidate)) return false; }
    mount_len = strlen(mount_point); if (strncmp(relative_root, mount_point, mount_len) == 0) { relative_root += mount_len; while (*relative_root == '/') ++relative_root; }
    return snprintf(out_src, out_src_size, "%c:%s/%s/%s", (char)LV_FS_STDIO_LETTER, relative_root, pack_id, asset_name) < (int)out_src_size;
}

static void set_gif_placeholder(const char *text)
{
    static const char *ascii_idle[] = {" /\\_/\\\\\n( o.o )\n > ^ <", " /\\_/\\\\\n( -.- )\n > ^ <", " /\\_/\\\\\n( o.o )\n  /|\\ "};
    lv_gif_set_src(s_buddy.gif_obj, NULL); lv_image_set_scale(s_buddy.gif_obj, LV_SCALE_NONE); s_buddy.gif_running = false; s_buddy.gif_source_width = 0; s_buddy.gif_source_height = 0; s_buddy.gif_progress.frame_index = -1; s_buddy.gif_progress.frame_count = -1; s_buddy.gif_progress.source_width = 0; s_buddy.gif_progress.source_height = 0; s_buddy.gif_progress.source[0] = '\0';
    lv_obj_add_flag(s_buddy.gif_obj, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_buddy.gif_label, LV_OBJ_FLAG_HIDDEN);
    if (text != NULL && strcmp(text, ui_text(UI_TEXT_NO_PACK)) == 0) lv_label_set_text(s_buddy.gif_label, ascii_idle[(uint32_t)((esp_timer_get_time() / 500000) % 3)]); else lv_label_set_text(s_buddy.gif_label, text);
    s_buddy.gif_pack_id[0] = '\0'; s_buddy.gif_src[0] = '\0';
}

void buddy_app_ui_buddy_update_gif(bool have_active, const example_charpack_info_t *active_pack, const example_buddy_state_cache_t *state_cache, const esp_desktop_buddy_transport_ble_state_t *transport)
{
#if CONFIG_LV_USE_GIF
    char desired_src[BUDDY_APP_GIF_PATH_MAX]; const char *desired_asset = "sleep.gif"; int32_t source_width, source_height;
    if (!have_active || active_pack == NULL || active_pack->pack_id[0] == '\0' || state_cache == NULL || transport == NULL) { set_gif_placeholder(ui_text(UI_TEXT_NO_PACK)); return; }
    if (state_cache->prompt.present || transport->has_passkey) desired_asset = "attention.gif"; else if (transport->connected && state_cache->has_state && state_cache->running > 0) desired_asset = "busy.gif"; else if (transport->connected && state_cache->has_state) desired_asset = "idle.gif";
    if (!build_gif_src(active_pack->pack_id, desired_asset, desired_src, sizeof(desired_src))) { set_gif_placeholder(ui_text(UI_TEXT_GIF_MISSING)); return; }
    if (strcmp(s_buddy.gif_pack_id, active_pack->pack_id) == 0 && strcmp(s_buddy.gif_src, desired_src) == 0) return;
    lv_obj_clear_flag(s_buddy.gif_obj, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_buddy.gif_label, LV_OBJ_FLAG_HIDDEN); lv_gif_set_src(s_buddy.gif_obj, desired_src);
    if (!lv_gif_is_loaded(s_buddy.gif_obj)) { set_gif_placeholder(ui_text(UI_TEXT_GIF_FAILED)); return; }
    strlcpy(s_buddy.gif_pack_id, active_pack->pack_id, sizeof(s_buddy.gif_pack_id)); strlcpy(s_buddy.gif_src, desired_src, sizeof(s_buddy.gif_src)); source_width = lv_image_get_src_width(s_buddy.gif_obj); source_height = lv_image_get_src_height(s_buddy.gif_obj);
    if (source_width <= 0 || source_height <= 0) { set_gif_placeholder(ui_text(UI_TEXT_GIF_FAILED)); return; }
    s_buddy.gif_source_width = (uint32_t)source_width; s_buddy.gif_source_height = (uint32_t)source_height; s_buddy.gif_progress.frame_count = lv_gif_get_frame_count(s_buddy.gif_obj); s_buddy.gif_progress.source_width = s_buddy.gif_source_width; s_buddy.gif_progress.source_height = s_buddy.gif_source_height; strlcpy(s_buddy.gif_progress.source, desired_src, sizeof(s_buddy.gif_progress.source)); s_buddy.gif_running = true; fit_gif_to_card();
    ESP_LOGI("buddy_app_ui", "gif loaded src=%s size=%ldx%ld frames=%ld", s_buddy.gif_progress.source, (long)s_buddy.gif_progress.source_width, (long)s_buddy.gif_progress.source_height, (long)s_buddy.gif_progress.frame_count);
#else
    (void)have_active; (void)active_pack; (void)state_cache; (void)transport; set_gif_placeholder(ui_text(UI_TEXT_GIF_OFF));
#endif
}

void buddy_app_ui_buddy_set_gif_playing(bool playing) { if (s_buddy.gif_obj == NULL || !lv_gif_is_loaded(s_buddy.gif_obj) || s_buddy.gif_running == playing) return; if (playing) lv_gif_resume(s_buddy.gif_obj); else lv_gif_pause(s_buddy.gif_obj); s_buddy.gif_running = playing; }
bool buddy_app_ui_buddy_gif_loaded(void) { return s_buddy.gif_obj != NULL && lv_gif_is_loaded(s_buddy.gif_obj); }
void buddy_app_ui_buddy_reset_gif(void) { s_buddy.gif_pack_id[0] = '\0'; s_buddy.gif_src[0] = '\0'; s_buddy.gif_running = false; s_buddy.gif_source_width = 0; s_buddy.gif_source_height = 0; s_buddy.gif_progress.frame_index = -1; s_buddy.gif_progress.frame_count = -1; s_buddy.gif_progress.source_width = 0; s_buddy.gif_progress.source_height = 0; s_buddy.gif_progress.sample_time_us = 0; s_buddy.gif_progress.running = false; s_buddy.gif_progress.loaded = false; s_buddy.gif_progress.source[0] = '\0'; }
void buddy_app_ui_buddy_sample_gif_progress(buddy_app_ui_buddy_gif_progress_t *progress) { if (progress == NULL) return; s_buddy.gif_progress.loaded = buddy_app_ui_buddy_gif_loaded(); s_buddy.gif_progress.running = s_buddy.gif_running; s_buddy.gif_progress.frame_index = s_buddy.gif_progress.loaded ? lv_gif_get_current_frame_index(s_buddy.gif_obj) : -1; s_buddy.gif_progress.sample_time_us = esp_timer_get_time(); *progress = s_buddy.gif_progress; }
