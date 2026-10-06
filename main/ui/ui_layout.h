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
    bool landscape;
    int32_t screen_width;
    int32_t screen_height;
    int32_t content_width;
    int32_t content_height;
    int32_t nav_size;
    int32_t status_height;
    int32_t inset;
    int32_t gap;
    int32_t top;
    int32_t card_width;
} buddy_app_ui_layout_t;

bool buddy_app_ui_layout_get(lv_display_t *display, buddy_app_ui_layout_t *layout);
