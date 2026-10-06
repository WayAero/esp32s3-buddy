/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lvgl.h"
#include "app_shared.h"

typedef struct {
    lv_event_cb_t track_tap_event;
    lv_event_cb_t pack_selector_event;
    bool (*interaction_allowed)(void *context);
    void (*input_activity)(void *context);
    bool (*tap_is_click)(lv_event_t *event, void *context);
    const char *(*localized_text)(const char *en, const char *zh, void *context);
    void (*request_theme)(buddy_app_theme_t theme, void *context);
    void (*request_rotation)(buddy_app_rotation_mode_t mode, void *context);
    void (*open_diagnostics)(void *context);
    /* 选择器已应用目标语言；回调只负责刷新和保存。 */
    void (*language_changed)(void *context);
    void (*set_brightness)(uint8_t percent, void *context);
    esp_err_t (*request_reset)(void *context);
    void (*request_save)(void *context);
    void (*notify)(uint32_t dirty, void *context);
    void *context;
} buddy_app_ui_settings_callbacks_t;

lv_obj_t *buddy_app_ui_settings_root(void);
bool buddy_app_ui_settings_create(lv_obj_t *parent,
                                  buddy_app_t *app,
                                  const buddy_app_ui_settings_callbacks_t *callbacks);
void buddy_app_ui_settings_layout(bool landscape, int32_t content_height,
                                  int32_t inset, int32_t top, int32_t card_width);
void buddy_app_ui_settings_close_name_dialog(void);
void buddy_app_ui_settings_close_reset_dialog(void);
void buddy_app_ui_settings_refresh(const buddy_app_ui_snapshot_t *snapshot,
                                   bool interaction_blocked);
void buddy_app_ui_settings_apply_locale(void);

bool buddy_app_ui_settings_create_pack_selector(lv_obj_t *parent, lv_event_cb_t event_cb);
lv_obj_t *buddy_app_ui_settings_pack_caption_label(void);
bool buddy_app_ui_settings_pack_selector_exists(void);
uint32_t buddy_app_ui_settings_pack_selector_selected(void);
void buddy_app_ui_settings_invalidate_pack_selector(void);
void buddy_app_ui_settings_set_pack_detail(const char *text);
void buddy_app_ui_settings_update_pack_selector(const buddy_app_ui_snapshot_t *snapshot,
                                             bool switch_request_failed);
