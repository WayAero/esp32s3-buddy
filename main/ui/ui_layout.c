/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_layout.h"

bool buddy_app_ui_layout_get(lv_display_t *display, buddy_app_ui_layout_t *layout)
{
    if (display == NULL || layout == NULL) {
        return false;
    }

    layout->screen_width = lv_display_get_horizontal_resolution(display);
    layout->screen_height = lv_display_get_vertical_resolution(display);
    layout->landscape = layout->screen_width > layout->screen_height;
    layout->nav_size = 52;
    layout->status_height = 28;
    layout->inset = 10;
    layout->gap = 8;
    layout->content_width = layout->landscape ? layout->screen_width - layout->nav_size :
                                                layout->screen_width;
    layout->content_height = layout->landscape ? layout->screen_height :
                                                 layout->screen_height - layout->nav_size;
    layout->top = layout->status_height + 4;
    layout->card_width = layout->content_width - layout->inset * 2;
    return true;
}
