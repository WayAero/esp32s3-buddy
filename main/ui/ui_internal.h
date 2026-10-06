/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "app_shared.h"
#include "lvgl.h"

typedef enum {
    BUDDY_APP_PAGE_HOME = 0,
    BUDDY_APP_PAGE_PACK,
    BUDDY_APP_PAGE_DIAGNOSTICS,
    BUDDY_APP_PAGE_ACTIVITY,
    BUDDY_APP_PAGE_SETTINGS,
    BUDDY_APP_PAGE_COUNT,
} buddy_app_ui_page_t;

typedef struct {
    buddy_app_t *app;
    lv_display_t *display;
} buddy_app_ui_context_t;
