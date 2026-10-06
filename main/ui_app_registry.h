/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_locale.h"

typedef enum {
    UI_APP_ID_BUDDY = 0,
    UI_APP_ID_DIAGNOSTICS,
    UI_APP_ID_COUNT,
} ui_app_id_t;

#include "app_shared.h"

#define UI_APP_CAP_BLE BUDDY_APP_CAP_BLE
#define UI_APP_CAP_STORAGE BUDDY_APP_CAP_STORAGE
#define UI_APP_CAP_TIME BUDDY_APP_CAP_TIME

typedef bool (*ui_app_create_cb_t)(void *context);
typedef void (*ui_app_lifecycle_cb_t)(void *context);
typedef void (*ui_app_event_cb_t)(void *context, uint32_t event);

typedef struct {
    ui_app_id_t id;
    ui_text_id_t title_id;
    ui_text_id_t subtitle_id;
    const char *icon_symbol;
    uint32_t icon_bg;
    uint32_t icon_color;
    uint32_t required_caps;
    uint32_t required_any_caps;
    uint8_t page;
    void **page_slot;
    ui_app_create_cb_t create;
    ui_app_lifecycle_cb_t on_show;
    ui_app_lifecycle_cb_t on_hide;
    ui_app_event_cb_t on_event;
    ui_app_lifecycle_cb_t on_locale_changed;
} ui_app_descriptor_t;

const ui_app_descriptor_t *ui_app_registry_get(size_t *count);
const ui_app_descriptor_t *ui_app_registry_find(ui_app_id_t id);
const ui_app_descriptor_t *ui_app_registry_find_by_page(uint8_t page);
bool ui_app_registry_is_available(const ui_app_descriptor_t *app, uint32_t available_caps);
bool ui_app_registry_bind(ui_app_id_t id,
                          uint8_t page,
                          void **page_slot,
                          ui_app_create_cb_t create,
                          ui_app_lifecycle_cb_t on_show,
                          ui_app_lifecycle_cb_t on_hide,
                          ui_app_event_cb_t on_event,
                          ui_app_lifecycle_cb_t on_locale_changed);
