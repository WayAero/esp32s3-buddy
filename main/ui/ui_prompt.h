/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef struct {
    lv_event_cb_t track_tap_event;
    lv_event_cb_t action_event;
    uintptr_t allow_action;
    uintptr_t deny_action;
} buddy_app_ui_prompt_callbacks_t;

bool buddy_app_ui_prompt_create(lv_obj_t *parent,
                                const buddy_app_ui_prompt_callbacks_t *callbacks);
lv_obj_t *buddy_app_ui_prompt_root(void);
void buddy_app_ui_prompt_layout(int32_t screen_width,
                                int32_t screen_height,
                                int32_t inset,
                                int32_t gap);
void buddy_app_ui_prompt_show(void);
void buddy_app_ui_prompt_hide(void);
void buddy_app_ui_prompt_apply_locale_fonts(void);
void buddy_app_ui_prompt_refresh(bool passkey_active,
                                 bool prompt_active,
                                 const char *title,
                                 const char *body,
                                 const char *detail,
                                 bool reset_scroll);
void buddy_app_ui_prompt_set_action_state(bool prompt_active, bool reply_submitted);
void buddy_app_ui_prompt_set_attention_anim(bool active);
