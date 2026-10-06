/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "app_shared.h"
#include "lvgl.h"

void buddy_app_ui_status_bar_create(lv_obj_t *root);
void buddy_app_ui_status_bar_layout(int32_t content_width, int32_t status_height);
void buddy_app_ui_status_bar_set_visible(bool visible);

void buddy_app_ui_status_bar_refresh(bool ble_connected,
                                     const char *time_text,
                                     lv_color_t connected_color,
                                     lv_color_t disconnected_color);
