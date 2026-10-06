/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_app_registry.h"

#include <stddef.h>

/* Font Awesome codepoints included in LVGL's built-in symbol font. */
#define UI_APP_SYMBOL_BLUETOOTH "\xEF\x8A\x93"
#define UI_APP_SYMBOL_TINT "\xEF\x81\x83"
#define UI_APP_SYMBOL_REFRESH "\xEF\x80\xA1"
#define UI_APP_SYMBOL_LIST "\xEF\x80\x8B"

static ui_app_descriptor_t s_ui_apps[] = {
    {
        .id = UI_APP_ID_BUDDY,
        .title_id = UI_TEXT_BUDDY,
        .subtitle_id = UI_TEXT_APP_BUDDY_SUBTITLE,
        .icon_symbol = UI_APP_SYMBOL_BLUETOOTH,
        .icon_bg = 0x173A61,
        .icon_color = 0xB9DCFF,
        .required_caps = 0,
        .required_any_caps = 0,
        .page = UINT8_MAX,
        .page_slot = NULL,
        .create = NULL, .on_show = NULL, .on_hide = NULL, .on_event = NULL, .on_locale_changed = NULL,
    },
    {
        .id = UI_APP_ID_DIAGNOSTICS,
        .title_id = UI_TEXT_DIAGNOSTICS,
        .subtitle_id = UI_TEXT_APP_DIAGNOSTICS_SUBTITLE,
        .icon_symbol = UI_APP_SYMBOL_LIST,
        .icon_bg = 0x303137,
        .icon_color = 0xD4D4D8,
        .required_caps = 0,
        .required_any_caps = 0,
        .page = UINT8_MAX,
        .page_slot = NULL,
        .create = NULL, .on_show = NULL, .on_hide = NULL, .on_event = NULL, .on_locale_changed = NULL,
    },
};

const ui_app_descriptor_t *ui_app_registry_get(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(s_ui_apps) / sizeof(s_ui_apps[0]);
    }
    return s_ui_apps;
}

const ui_app_descriptor_t *ui_app_registry_find(ui_app_id_t id)
{
    for (size_t i = 0; i < sizeof(s_ui_apps) / sizeof(s_ui_apps[0]); ++i) {
        if (s_ui_apps[i].id == id) {
            return &s_ui_apps[i];
        }
    }
    return NULL;
}

const ui_app_descriptor_t *ui_app_registry_find_by_page(uint8_t page)
{
    for (size_t i = 0; i < sizeof(s_ui_apps) / sizeof(s_ui_apps[0]); ++i) {
        if (s_ui_apps[i].page == page) {
            return &s_ui_apps[i];
        }
    }
    return NULL;
}

bool ui_app_registry_is_available(const ui_app_descriptor_t *app, uint32_t available_caps)
{
    return app != NULL &&
           (app->required_caps & ~available_caps) == 0 &&
           (app->required_any_caps == 0 || (app->required_any_caps & available_caps) != 0);
}

bool ui_app_registry_bind(ui_app_id_t id,
                          uint8_t page,
                          void **page_slot,
                          ui_app_create_cb_t create,
                          ui_app_lifecycle_cb_t on_show,
                          ui_app_lifecycle_cb_t on_hide,
                          ui_app_event_cb_t on_event,
                          ui_app_lifecycle_cb_t on_locale_changed)
{
    for (size_t i = 0; i < sizeof(s_ui_apps) / sizeof(s_ui_apps[0]); ++i) {
        if (s_ui_apps[i].id == id) {
            s_ui_apps[i].page = page;
            s_ui_apps[i].page_slot = page_slot;
            s_ui_apps[i].create = create;
            s_ui_apps[i].on_show = on_show;
            s_ui_apps[i].on_hide = on_hide;
            s_ui_apps[i].on_event = on_event;
            s_ui_apps[i].on_locale_changed = on_locale_changed;
            return true;
        }
    }
    return false;
}
