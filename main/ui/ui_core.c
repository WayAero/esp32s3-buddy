/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_theme.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_shared.h"
#include "example_app_helpers.h"
#include "ui_app_registry.h"
#include "ui/ui_activity.h"
#include "ui/ui_aod.h"
#include "ui/ui_buddy.h"
#include "ui/ui_diagnostics.h"
#include "ui/ui_format.h"
#include "ui/ui_home.h"
#include "ui/ui_prompt.h"
#include "ui/ui_settings.h"
#include "ui/ui_status_bar.h"
#include "ui/ui_internal.h"
#include "ui/ui_layout.h"
#include "ui/ui_navigation.h"
#include "ui_locale.h"

#define BUDDY_APP_UI_STACK 8192
#define BUDDY_APP_UI_PRIORITY 4
#define BUDDY_APP_UI_CLOCK_REFRESH_MS 1000
#define BUDDY_APP_UI_LOCK_TIMEOUT_MS 1000
#define BUDDY_APP_UI_STATE_LOCK_TIMEOUT_MS 20
#define BUDDY_APP_UI_AOD_DEFAULT_MINUTES 30
#define BUDDY_APP_UI_AOD_STEP_MINUTES 5
#define BUDDY_APP_UI_ROTATE_TOUCH_GUARD_US 80000LL
#define BUDDY_APP_UI_START_TOUCH_GUARD_US 150000LL
#define BUDDY_APP_UI_LOCK_RETRY_MS 20
#define BUDDY_APP_UI_TAP_SLOP_PX 12

#define BUDDY_APP_UI_CORE CONFIG_BUDDY_LVGL_CORE

#if CONFIG_LV_FONT_MONTSERRAT_14
#define BUDDY_APP_FONT_BODY (&lv_font_montserrat_14)
#else
#define BUDDY_APP_FONT_BODY LV_FONT_DEFAULT
#endif

#if CONFIG_LV_FONT_MONTSERRAT_24
#define BUDDY_APP_FONT_PASSKEY (&lv_font_montserrat_24)
#else
#define BUDDY_APP_FONT_PASSKEY BUDDY_APP_FONT_BODY
#endif

#define BUDDY_APP_FONT_ACTION BUDDY_APP_FONT_BODY
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

static uint32_t s_apps_available_caps;
static uint32_t s_app_create_failed_mask;
static bool s_pack_list_refresh_started;
static bool s_pack_list_refresh_requested;
static char s_pack_list_status[BUDDY_APP_STATUS_MAX];
static esp_err_t s_pack_switch_request_result = ESP_OK;
static lv_obj_t *s_tap_target;
static lv_point_t s_tap_start;
static bool s_tap_candidate;
static void buddy_app_apply_locale_fonts(void);
static void buddy_app_registry_on_show(void *context);
static void buddy_app_registry_on_hide(void *context);
static void buddy_app_registry_on_event(void *context, uint32_t event);
static void buddy_app_registry_on_locale_changed(void *context);
static bool buddy_app_descriptor_available(const ui_app_descriptor_t *descriptor,
                                           uint32_t available_caps);
static const char *buddy_app_localized_text(const char *en, const char *zh);
static const char *buddy_app_localized_text_callback(const char *en, const char *zh,
                                                      void *context);
static lv_obj_t *s_nav;
static buddy_app_t *s_ui_app;
static char s_displayed_prompt_id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];
static lv_display_t *s_display;
static bool s_prompt_forced_page;
static bool s_aod_active;
static buddy_app_ui_buddy_gif_progress_t s_gif_progress = {
    .frame_index = -1,
    .frame_count = -1,
};
static bool s_was_attention_active;
static int64_t s_last_activity_us;
static bsp_status_led_mode_t s_last_led_mode = BSP_STATUS_LED_MODE_NORMAL;
static lv_display_rotation_t s_active_rotation = LV_DISPLAY_ROTATION_0;
static int64_t s_touch_blocked_until_us;
static bool s_aod_brightness_applied;

static void buddy_app_style_label(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static void buddy_app_relayout(void)
{
    lv_obj_t *pages[] = {buddy_app_ui_settings_root(), buddy_app_ui_buddy_root(),
                         buddy_app_ui_diagnostics_root()};
    buddy_app_ui_layout_t layout;

    if (s_display == NULL || s_nav == NULL)
    {
        return;
    }
    if (!buddy_app_ui_layout_get(s_display, &layout))
    {
        return;
    }
    lv_obj_set_size(s_nav, layout.landscape ? layout.nav_size : layout.screen_width,
                    layout.landscape ? layout.screen_height : layout.nav_size);
    lv_obj_align(s_nav, layout.landscape ? LV_ALIGN_RIGHT_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s_nav, layout.landscape ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(s_nav, 4, 0);
    lv_obj_set_style_pad_gap(s_nav, 4, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_nav); ++i)
    {
        lv_obj_t *button = lv_obj_get_child(s_nav, i);

        lv_obj_set_size(button, layout.landscape ? LV_PCT(100) : 0,
                        layout.landscape ? 0 : LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
    }
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i)
    {
        if (pages[i] == NULL)
            continue;
        lv_obj_set_size(pages[i], layout.content_width, layout.content_height);
        lv_obj_set_pos(pages[i], 0, 0);
    }
    buddy_app_ui_home_layout(layout.landscape, layout.content_width, layout.content_height,
                             layout.status_height);
    buddy_app_ui_prompt_layout(layout.screen_width, layout.screen_height, layout.inset, layout.gap);
    buddy_app_ui_aod_layout(layout.screen_width, layout.screen_height, layout.landscape);
    buddy_app_ui_status_bar_layout(layout.content_width, layout.status_height);
    lv_obj_update_layout(lv_screen_active());
    int32_t top = layout.top;
    int32_t card_w = layout.card_width;
    int32_t content_h = layout.content_height;
    int32_t inset = layout.inset;
    int32_t gap = layout.gap;
    bool landscape = layout.landscape;

    buddy_app_ui_buddy_layout(landscape, content_h, inset, top, card_w);

    buddy_app_ui_diagnostics_layout(landscape, content_h, inset, gap, top, card_w);

    buddy_app_ui_activity_layout(landscape, layout.content_width, content_h, inset, top, card_w);

    buddy_app_ui_settings_layout(landscape, content_h, inset, top, card_w);

}

static void buddy_app_update_nav_state(buddy_app_ui_page_t page)
{
    static const buddy_app_ui_page_t nav_pages[] = {
        BUDDY_APP_PAGE_HOME,
        BUDDY_APP_PAGE_PACK,
        BUDDY_APP_PAGE_ACTIVITY,
        BUDDY_APP_PAGE_SETTINGS,
    };

    if (s_nav == NULL)
    {
        return;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_nav) && i < sizeof(nav_pages) / sizeof(nav_pages[0]); ++i)
    {
        lv_obj_t *button = lv_obj_get_child(s_nav, i);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        bool selected = page == nav_pages[i];

        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(selected ? BUDDY_APP_COLOR_ACCENT_SOFT : BUDDY_APP_COLOR_PANEL),
                                  0);
        lv_obj_set_style_border_width(button, selected ? BUDDY_APP_UI_BORDER_WIDTH : 0, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(BUDDY_APP_COLOR_ACCENT), 0);
        if (label != NULL)
        {
            lv_obj_set_style_text_color(label,
                                        lv_color_hex(selected ? BUDDY_APP_COLOR_ACCENT : BUDDY_APP_COLOR_MUTED),
                                        0);
        }
    }
}

static lv_obj_t *buddy_app_page_object(buddy_app_ui_page_t page)
{
    const ui_app_descriptor_t *descriptor = ui_app_registry_find_by_page((uint8_t)page);

    if (descriptor != NULL && descriptor->page_slot != NULL)
        return *(lv_obj_t **)descriptor->page_slot;
    switch (page)
    {
    case BUDDY_APP_PAGE_HOME: return buddy_app_ui_home_root();
    case BUDDY_APP_PAGE_ACTIVITY: return buddy_app_ui_activity_root();
    case BUDDY_APP_PAGE_SETTINGS: return buddy_app_ui_settings_root();
    default: return NULL;
    }
}

static bool buddy_app_descriptor_available(const ui_app_descriptor_t *descriptor,
                                           uint32_t available_caps)
{
    return ui_app_registry_is_available(descriptor, available_caps) &&
           (s_app_create_failed_mask & (1U << descriptor->id)) == 0;
}

static void buddy_app_apply_page(buddy_app_ui_page_t page)
{
    const ui_app_descriptor_t *descriptor = ui_app_registry_find_by_page((uint8_t)page);
    const ui_app_descriptor_t *previous = ui_app_registry_find_by_page((uint8_t)buddy_app_ui_navigation_rendered());
    bool created = false;

    if (descriptor != NULL && descriptor->create != NULL && buddy_app_page_object(page) == NULL)
    {
        if (!descriptor->create(lv_screen_active()))
        {
            s_app_create_failed_mask |= 1U << descriptor->id;
            page = BUDDY_APP_PAGE_HOME;
            descriptor = NULL;
        }
        else
        {
            created = true;
            bsp_memory_log_stage(descriptor->id == UI_APP_ID_BUDDY ? "first Buddy create" :
                                 descriptor->id == UI_APP_ID_DIAGNOSTICS ? "first Diagnostics create" :
                                 "app page created");
        }
    }
    if (created)
    {
        buddy_app_apply_locale_fonts();
        buddy_app_relayout();
        size_t count = 0;
        const ui_app_descriptor_t *apps = ui_app_registry_get(&count);
        bool all_created = true;
        for (size_t i = 0; i < count; ++i)
            if (apps[i].page_slot == NULL || *apps[i].page_slot == NULL) all_created = false;
        if (all_created) bsp_memory_log_stage("all app pages created");
    }
    if (previous != descriptor && previous != NULL && previous->on_hide != NULL)
        previous->on_hide((void *)previous);

    buddy_app_ui_navigation_set_visible(buddy_app_ui_home_root(), page == BUDDY_APP_PAGE_HOME);
    {
        const ui_app_descriptor_t *apps;
        size_t count = 0;

        apps = ui_app_registry_get(&count);
        for (size_t i = 0; i < count; ++i)
        {
            if (apps[i].page_slot != NULL)
                buddy_app_ui_navigation_set_visible(*(lv_obj_t **)apps[i].page_slot, page == apps[i].page);
        }
    }
    buddy_app_ui_navigation_set_visible(buddy_app_ui_activity_root(), page == BUDDY_APP_PAGE_ACTIVITY);
    buddy_app_ui_navigation_set_visible(buddy_app_ui_settings_root(), page == BUDDY_APP_PAGE_SETTINGS);
    if (page != BUDDY_APP_PAGE_SETTINGS || s_prompt_forced_page || s_aod_active)
    {
        buddy_app_ui_settings_close_name_dialog();
        buddy_app_ui_settings_close_reset_dialog();
    }
    if (s_prompt_forced_page && !s_aod_active)
        buddy_app_ui_prompt_show();
    else
        buddy_app_ui_prompt_hide();
    buddy_app_ui_navigation_set_visible(buddy_app_ui_aod_root(), s_aod_active);
    if (s_nav != NULL)
        buddy_app_ui_navigation_set_visible(s_nav, !s_aod_active);
    buddy_app_ui_status_bar_set_visible(!s_aod_active);
    if (s_prompt_forced_page && !s_aod_active)
        lv_obj_move_foreground(buddy_app_ui_prompt_root());
    if (s_aod_active)
        lv_obj_move_foreground(buddy_app_ui_aod_root());
    buddy_app_ui_buddy_set_gif_playing(page == BUDDY_APP_PAGE_PACK && !s_aod_active &&
                                        !s_prompt_forced_page);

    if (buddy_app_ui_navigation_rendered() != page)
    {
        buddy_app_update_nav_state(page);
        if (descriptor != NULL && descriptor->on_show != NULL && !s_aod_active && !s_prompt_forced_page)
            descriptor->on_show((void *)descriptor);
        buddy_app_ui_navigation_set_rendered(page);
    }
}

static void buddy_app_note_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
    s_aod_active = false;
}

static void buddy_app_refresh_activity(buddy_app_t *app)
{
    buddy_app_ui_activity_refresh(app);
}

static void buddy_app_refresh_diagnostics(buddy_app_t *app)
{
    buddy_app_ui_diagnostics_refresh(app,
                                     buddy_app_ui_navigation_current() == BUDDY_APP_PAGE_DIAGNOSTICS);
}


static void buddy_app_refresh_buddy(const example_buddy_state_cache_t *state,
                                    const char *status_text,
                                    lv_color_t status_color,
                                    const char *tokens_text,
                                    const char *context_text,
                                    const char *context_compact_text)
{
    buddy_app_ui_buddy_refresh(state, status_text, status_color, tokens_text,
                               context_text, context_compact_text);
}

static void buddy_app_refresh_home(const example_buddy_state_cache_t *state,
                                   bool has_snapshot,
                                   const char *title_text,
                                   const char *date_text,
                                   const char *transport_text,
                                   const char *buddy_status_text,
                                   lv_color_t buddy_status_color,
                                   const char *tokens_text,
                                   const char *context_text,
                                   const char *activity_text)
{
    buddy_app_ui_home_refresh(state, has_snapshot, title_text, date_text, transport_text,
                              buddy_status_text, buddy_status_color, tokens_text,
                              context_text, activity_text);
}

static void buddy_app_ui_format_context_metric(char *dst, size_t dst_size, uint64_t value)
{
    static const char suffixes[] = " KMGTPE";
    uint64_t scale = 1;
    size_t suffix_index = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    while (suffix_index < 6 && value / scale >= 1000U) {
        scale *= 1000U;
        suffix_index++;
    }
    if (suffix_index == 0) {
        snprintf(dst, dst_size, "%" PRIu64, value);
        return;
    }
    snprintf(dst, dst_size, "%" PRIu64 "%c", value / scale, suffixes[suffix_index]);
}

static void buddy_app_ui_format_context(char *dst,
                                        size_t dst_size,
                                        const example_buddy_state_cache_t *state,
                                        bool compact)
{
    char value_text[16];
    char window_text[16];
    uint64_t percent;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (state == NULL || !state->has_state || !state->context_value_valid) {
        strlcpy(dst, "--", dst_size);
        return;
    }

    buddy_app_ui_format_context_metric(value_text, sizeof(value_text), state->context_value);
    if (!state->context_window_valid) {
        snprintf(dst, dst_size, compact ? "~%s/--" : "~%s / --", value_text);
        return;
    }

    buddy_app_ui_format_context_metric(window_text, sizeof(window_text), state->context_window);
    percent = 100U * (state->context_value / state->context_window) +
              100U * (state->context_value % state->context_window) / state->context_window;
    snprintf(dst,
             dst_size,
             compact ? "~%s/%s %" PRIu64 "%%" : "~%s / %s (%" PRIu64 "%%)",
             value_text,
             window_text,
             percent);
}

static void buddy_app_refresh_prompt(bool passkey_active,
                                     bool prompt_active,
                                     const char *title,
                                     const char *body,
                                     const char *detail,
                                     bool reset_scroll)
{
    buddy_app_ui_prompt_refresh(passkey_active, prompt_active, title, body, detail,
                                reset_scroll);
}

static void buddy_app_refresh_aod(const char *time_text,
                                  const char *second_text,
                                  const char *date_text,
                                  const char *status_text)
{
    buddy_app_ui_aod_refresh(time_text, second_text, date_text, status_text);
}


static void buddy_app_refresh_status_bar(bool ble_connected,
                                         const char *time_text)
{
    buddy_app_ui_status_bar_refresh(ble_connected,
                                    time_text,
                                    lv_color_hex(BUDDY_APP_COLOR_ACCENT),
                                    lv_color_hex(BUDDY_APP_COLOR_MUTED));
}

static void buddy_app_navigate_to(buddy_app_ui_page_t page)
{
    buddy_app_note_activity();
    if (buddy_app_ui_navigation_current() == page && buddy_app_ui_navigation_rendered() == page)
    {
        return;
    }
    buddy_app_ui_navigation_go(page);
    /* This runs in an LVGL event callback; the UI task already owns the lock. */
    buddy_app_apply_page(page);
}

typedef enum
{
    BUDDY_ACTION_HOME = BUDDY_APP_PAGE_HOME,
    BUDDY_ACTION_BUDDY = BUDDY_APP_PAGE_PACK,
    BUDDY_ACTION_ACTIVITY = BUDDY_APP_PAGE_ACTIVITY,
    BUDDY_ACTION_SETTINGS = BUDDY_APP_PAGE_SETTINGS,
    BUDDY_ACTION_ALLOW,
    BUDDY_ACTION_DENY,
} buddy_app_action_t;

static void buddy_app_open_registered_app(ui_app_id_t app_id)
{
    const ui_app_descriptor_t *descriptor = ui_app_registry_find(app_id);

    if (s_ui_app == NULL || s_aod_active || s_prompt_forced_page ||
        esp_timer_get_time() < s_touch_blocked_until_us ||
        !buddy_app_descriptor_available(descriptor, s_apps_available_caps))
        return;
    buddy_app_navigate_to((buddy_app_ui_page_t)descriptor->page);
}

static void buddy_app_dispatch_action(buddy_app_action_t action)
{
    buddy_app_t *app = s_ui_app;
    static EXT_RAM_BSS_ATTR buddy_app_ui_snapshot_t snapshot;
    esp_desktop_buddy_permission_decision_t decision;

    if (app == NULL || esp_timer_get_time() < s_touch_blocked_until_us)
        return;
    if (s_aod_active)
    {
        lv_indev_t *indev = lv_indev_active();

        if (!buddy_app_ui_snapshot_get(app, &snapshot))
        {
            return;
        }
        s_aod_active = false;
        s_last_activity_us = esp_timer_get_time();
        if (bsp_display_set_brightness(snapshot.display_brightness_percent) == ESP_OK)
        {
            s_aod_brightness_applied = false;
        }
        if (indev != NULL)
        {
            lv_indev_wait_release(indev);
        }
        buddy_app_apply_page(buddy_app_ui_navigation_current());
        return;
    }
    switch (action)
    {
    case BUDDY_ACTION_HOME:
        buddy_app_navigate_to(BUDDY_APP_PAGE_HOME);
        return;
    case BUDDY_ACTION_BUDDY:
        buddy_app_open_registered_app(UI_APP_ID_BUDDY);
        return;
    case BUDDY_ACTION_ACTIVITY:
        buddy_app_navigate_to(BUDDY_APP_PAGE_ACTIVITY);
        return;
    case BUDDY_ACTION_SETTINGS:
        if (buddy_app_ui_navigation_current() != BUDDY_APP_PAGE_SETTINGS)
        {
            buddy_app_ui_navigation_set_page_before_settings(buddy_app_ui_navigation_current());
        }
        buddy_app_navigate_to(BUDDY_APP_PAGE_SETTINGS);
        return;
    case BUDDY_ACTION_ALLOW:
        if (s_displayed_prompt_id[0] == '\0')
        {
            return;
        }
        decision = ESP_DESKTOP_BUDDY_PERMISSION_DECISION_ONCE;
        break;
    case BUDDY_ACTION_DENY:
        if (s_displayed_prompt_id[0] == '\0')
        {
            return;
        }
        decision = ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY;
        break;
    default:
        return;
    }
    if (buddy_app_prompt_reply_enqueue(app, decision, s_displayed_prompt_id) == ESP_OK)
    {
        buddy_app_ui_prompt_set_action_state(true, true);
    }
}

static void buddy_app_track_tap_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_target_obj(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;

    if (code == LV_EVENT_PRESSED)
    {
        s_tap_target = target;
        s_tap_candidate = indev != NULL;
        if (indev != NULL)
        {
            lv_indev_get_point(indev, &s_tap_start);
        }
        return;
    }
    if (target != s_tap_target)
    {
        return;
    }
    if (code == LV_EVENT_PRESS_LOST)
    {
        s_tap_candidate = false;
        return;
    }
    if ((code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) &&
        s_tap_candidate && indev != NULL)
    {
        int32_t dx;
        int32_t dy;

        /* LVGL has already classified this gesture as a scroll. Keep that
         * classification even if the finger pauses before release. */
        if (lv_indev_get_scroll_obj(indev) != NULL)
        {
            s_tap_candidate = false;
            return;
        }
        lv_indev_get_point(indev, &point);
        dx = point.x - s_tap_start.x;
        dy = point.y - s_tap_start.y;
        if (dx * dx + dy * dy > BUDDY_APP_UI_TAP_SLOP_PX * BUDDY_APP_UI_TAP_SLOP_PX)
        {
            s_tap_candidate = false;
        }
    }
}

static bool buddy_app_tap_event_is_click(lv_event_t *event)
{
    return lv_event_get_code(event) == LV_EVENT_CLICKED &&
           lv_event_get_target_obj(event) == s_tap_target && s_tap_candidate;
}

static void buddy_app_action_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && !buddy_app_tap_event_is_click(event))
    {
        return;
    }
    buddy_app_dispatch_action((buddy_app_action_t)(uintptr_t)lv_event_get_user_data(event));
}

static void buddy_app_app_card_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    ui_app_id_t app_id = (ui_app_id_t)(uintptr_t)lv_event_get_user_data(event);

    buddy_app_track_tap_event(event);
    if (code != LV_EVENT_CLICKED || !buddy_app_tap_event_is_click(event) ||
        s_ui_app == NULL || s_aod_active || s_prompt_forced_page ||
        esp_timer_get_time() < s_touch_blocked_until_us)
    {
        return;
    }
    ESP_LOGI("buddy_app_ui", "touch open app id=%u", (unsigned)app_id);
    buddy_app_open_registered_app(app_id);
}

static void buddy_app_pack_selector_update(const buddy_app_ui_snapshot_t *snapshot)
{
    if (!buddy_app_ui_settings_pack_selector_exists() || s_ui_app == NULL || snapshot == NULL) return;
    if (!s_pack_list_refresh_started && !snapshot->charpack_list_pending)
    {
        strlcpy(s_pack_list_status, snapshot->pack_status, sizeof(s_pack_list_status));
        if (snapshot->charpack_list_result == ESP_ERR_INVALID_STATE)
        {
            s_pack_list_refresh_requested = true;
        }
        s_pack_list_refresh_started = true;
    }
    buddy_app_ui_settings_update_pack_selector(snapshot, s_pack_switch_request_result != ESP_OK);
}

static void buddy_app_pack_selector_event(lv_event_t *event)
{
    buddy_app_t *app = s_ui_app;
    static EXT_RAM_BSS_ATTR buddy_app_ui_snapshot_t snapshot;
    uint32_t selected;

    (void)event;
    if (app == NULL || s_aod_active || s_prompt_forced_page ||
        esp_timer_get_time() < s_touch_blocked_until_us)
    {
        return;
    }
    if (!buddy_app_ui_snapshot_get(app, &snapshot))
    {
        return;
    }
    selected = buddy_app_ui_settings_pack_selector_selected();
    if (!snapshot.charpack_list_pending && snapshot.charpack_list_result != ESP_OK)
    {
        (void)buddy_app_charpack_refresh_async(app);
        return;
    }
    if (snapshot.charpack_list_pending || snapshot.charpack_switch_pending ||
        selected >= snapshot.installed_pack_count ||
        selected >= BUDDY_APP_CHARPACK_LIST_MAX)
    {
        return;
    }
    buddy_app_note_activity();
    s_pack_switch_request_result = buddy_app_charpack_set_active_async(app,
                                                                         snapshot.installed_packs[selected].pack_id);
}

static lv_obj_t *buddy_app_create_touch_button(lv_obj_t *parent, const char *text,
                                               buddy_app_action_t action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(BUDDY_APP_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_add_event_cb(button, buddy_app_track_tap_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(button, buddy_app_action_event, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    buddy_app_style_label(label,
                          parent == s_nav ? LV_FONT_DEFAULT : BUDDY_APP_FONT_META,
                          lv_color_hex(BUDDY_APP_COLOR_MUTED));
    lv_obj_center(label);
    return button;
}

static void buddy_app_apply_locale_fonts(void)
{
    buddy_app_ui_home_apply_locale_fonts();
    buddy_app_ui_activity_apply_locale_fonts();
    buddy_app_ui_prompt_apply_locale_fonts();
    buddy_app_ui_buddy_apply_locale_fonts();
    buddy_app_ui_diagnostics_apply_locale_fonts();
    buddy_app_ui_aod_apply_locale_fonts();
    buddy_app_ui_settings_apply_locale();

}
static void buddy_app_apply_manual_rotation(buddy_app_rotation_mode_t mode, bool attention, int64_t now_us)
{
    if (s_display == NULL || attention) return;
    lv_display_rotation_t desired = mode == BUDDY_APP_ROTATION_LANDSCAPE ?
                                    LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_0;
    if (desired == s_active_rotation) return;
    esp_err_t err = bsp_display_set_rotation_locked(desired);
    if (err == ESP_OK) {
        s_active_rotation = desired;
        s_touch_blocked_until_us = now_us + BUDDY_APP_UI_ROTATE_TOUCH_GUARD_US;
        buddy_app_relayout();
        lv_obj_invalidate(lv_screen_active());
    } else ESP_LOGW("buddy_app_ui", "display rotation failed: %s", esp_err_to_name(err));
}


static void buddy_app_prompt_detail_append(char *detail, size_t detail_size, const char *line)
{
    if (line == NULL || line[0] == '\0')
        return;
    if (detail[0] != '\0')
        strlcat(detail, "\n", detail_size);
    strlcat(detail, line, detail_size);
}

static void buddy_app_ui_refresh(buddy_app_t *app,
                                 const buddy_app_ui_snapshot_t *snapshot)
{
    static ui_locale_t applied_locale = UI_LOCALE_COUNT;
    static EXT_RAM_BSS_ATTR example_buddy_state_cache_t state_cache;
    static EXT_RAM_BSS_ATTR esp_desktop_buddy_transport_ble_state_t transport;
    static EXT_RAM_BSS_ATTR example_charpack_info_t active_pack;
    uint32_t aod_timeout_minutes;
    ui_locale_t current_locale = ui_locale_get();
    bool have_active;
    bool passkey_active;
    bool prompt_active;
    bool prompt_changed = false;
    bool attention_active;
    bool home_buddy_has_snapshot;
    int64_t now_us = esp_timer_get_time();
    bool aod_active;
    char title_text[BUDDY_APP_NAME_MAX + BUDDY_APP_OWNER_MAX + 20];
    char home_time_text[16];
    char aod_time_text[8];
    char home_date_text[32];
    char clock_second_text[8];
    char transport_text[128];
    char tokens_text[24] = "--";
    char context_text[48] = "--";
    char context_compact_text[32] = "--";
    char home_activity_text[96];
    char pack_detail[128];
    char prompt_title[48];
    static EXT_RAM_BSS_ATTR char prompt_body[EXAMPLE_BUDDY_PROMPT_HINT_MAX + 1];
    char prompt_detail[256];
    /* Refresh runs only in the UI task; persistent scratch avoids stack spikes. */
    lv_color_t transport_color;
    const char *buddy_status_text = ui_text(UI_TEXT_DISCONNECTED);

    if (snapshot == NULL)
    {
        return;
    }

    buddy_ui_theme_apply(s_display, snapshot->theme);
    state_cache = snapshot->state_cache;
    transport = snapshot->transport_state;
    active_pack = snapshot->active_pack;
    have_active = snapshot->have_active_pack;
    s_apps_available_caps = snapshot->available_caps;
    aod_timeout_minutes = snapshot->aod_timeout_minutes != 0 ?
                          snapshot->aod_timeout_minutes :
                          BUDDY_APP_UI_AOD_DEFAULT_MINUTES;
    prompt_active = state_cache.has_state && state_cache.prompt.present;
    /* The transport may be connected before the first usable snapshot arrives.
     * Keep the home card in its explicit empty state during that interval. */
    home_buddy_has_snapshot = transport.tx_ready && state_cache.has_state;
    if (prompt_active)
    {
        prompt_changed = strcmp(s_displayed_prompt_id, state_cache.prompt.id) != 0;
        strlcpy(s_displayed_prompt_id,
                state_cache.prompt.id,
                sizeof(s_displayed_prompt_id));
    }
    else
    {
        s_displayed_prompt_id[0] = '\0';
    }
    passkey_active = transport.has_passkey;
    attention_active = prompt_active || passkey_active;
    buddy_app_apply_manual_rotation(snapshot->rotation_mode, attention_active, now_us);
    if (s_last_activity_us == 0)
    {
        s_last_activity_us = now_us;
    }
    if (attention_active && !s_was_attention_active)
    {
        s_aod_active = false;
        s_prompt_forced_page = true;
        s_last_activity_us = now_us;
    }
    else if (!attention_active && s_was_attention_active && s_prompt_forced_page)
    {
        s_prompt_forced_page = false;
    }
    if (snapshot->aod_enabled && !attention_active &&
        now_us - s_last_activity_us >=
            (int64_t)aod_timeout_minutes * 60LL * 1000000LL)
    {
        s_aod_active = true;
    }
    s_was_attention_active = attention_active;
    aod_active = s_aod_active;
    if (aod_active && !s_aod_brightness_applied)
    {
        if (bsp_display_set_brightness(snapshot->aod_brightness_percent) == ESP_OK)
        {
            s_aod_brightness_applied = true;
        }
    }
    else if (!aod_active && s_aod_brightness_applied)
    {
        if (bsp_display_set_brightness(snapshot->display_brightness_percent) == ESP_OK)
        {
            s_aod_brightness_applied = false;
        }
    }

    buddy_app_ui_format_clock(snapshot->time_source != BUDDY_APP_TIME_UNSYNCED,
                           snapshot->tz_offset_seconds, home_time_text,
                           sizeof(home_time_text),
                           home_date_text,
                           sizeof(home_date_text),
                           clock_second_text,
                           sizeof(clock_second_text));
    snprintf(aod_time_text, sizeof(aod_time_text), "%.5s", home_time_text);
    strlcpy(title_text, home_time_text, sizeof(title_text));
    buddy_app_refresh_status_bar(transport.connected, aod_time_text);

    {
        const char *status;

        if (prompt_active)
        {
            status = ui_text(UI_TEXT_APPROVAL_NEEDED);
            transport_color = lv_color_hex(BUDDY_APP_COLOR_ALLOW);
        }
        else if (passkey_active)
        {
            status = ui_text(UI_TEXT_PAIRING_CODE);
            transport_color = lv_color_hex(BUDDY_APP_COLOR_TEXT);
        }
        else if (transport.connected && !transport.tx_ready)
        {
            status = ui_text(UI_TEXT_SECURING);
            transport_color = lv_color_hex(BUDDY_APP_COLOR_MUTED);
        }
        else if (transport.tx_ready && state_cache.running > 0)
        {
            status = ui_text(UI_TEXT_WORKING);
            transport_color = lv_color_hex(BUDDY_APP_COLOR_ALLOW);
        }
        else if (transport.tx_ready)
        {
            status = ui_text(UI_TEXT_IDLE);
            transport_color = lv_color_hex(BUDDY_APP_COLOR_MUTED);
        }
        else
        {
            status = ui_text(UI_TEXT_DISCONNECTED);
            transport_color = lv_color_hex(BUDDY_APP_COLOR_MUTED);
        }
        buddy_status_text = status;
        strlcpy(transport_text, ui_text(UI_TEXT_BUDDY), sizeof(transport_text));
    }

    if (home_buddy_has_snapshot)
    {
        buddy_app_ui_format_compact_u64(tokens_text, sizeof(tokens_text), state_cache.tokens);
        buddy_app_ui_format_context(context_text, sizeof(context_text), &state_cache, false);
        buddy_app_ui_format_context(context_compact_text,
                                    sizeof(context_compact_text),
                                    &state_cache,
                                    true);
    }
    if (have_active)
    {
        strlcpy(pack_detail, active_pack.pack_id, sizeof(pack_detail));
    }
    else
    {
        strlcpy(pack_detail, ui_text(UI_TEXT_NO_PACK), sizeof(pack_detail));
    }

    if (passkey_active)
    {
        strlcpy(prompt_title, ui_text(UI_TEXT_PASSKEY), sizeof(prompt_title));
        snprintf(prompt_body,
                 sizeof(prompt_body),
                 "%06lu",
                 (unsigned long)transport.passkey);
        strlcpy(prompt_detail, ui_text(UI_TEXT_ENTER_ON_COMPUTER), sizeof(prompt_detail));
    }
    else if (prompt_active)
    {
        const bool has_status_detail = state_cache.msg[0] != '\0' &&
                                       strcmp(state_cache.msg, "approval") != 0;

        snprintf(prompt_title,
                 sizeof(prompt_title),
                 ui_text(UI_TEXT_REQUEST_FMT),
                 state_cache.prompt.tool[0] ? state_cache.prompt.tool : ui_text(UI_TEXT_APPROVAL));
        buddy_app_ui_copy_or_default(prompt_body,
                                  sizeof(prompt_body),
                                  state_cache.prompt.hint,
                                   has_status_detail ? state_cache.msg :
                                                       ui_text(UI_TEXT_APPROVE_REQUEST));
        prompt_detail[0] = '\0';
        if (state_cache.prompt.hint_truncated)
            buddy_app_prompt_detail_append(prompt_detail, sizeof(prompt_detail),
                                           ui_text(UI_TEXT_PROMPT_INCOMPLETE));
        if (snapshot->prompt_reply_failed)
            buddy_app_prompt_detail_append(prompt_detail, sizeof(prompt_detail),
                                           ui_text(UI_TEXT_REPLY_FAILED));
        if (state_cache.waiting > 1)
        {
            char waiting_text[40];

            snprintf(waiting_text,
                     sizeof(waiting_text),
                     ui_text(UI_TEXT_WAITING_COUNT_FMT),
                     (unsigned long)state_cache.waiting);
            buddy_app_prompt_detail_append(prompt_detail, sizeof(prompt_detail), waiting_text);
        }
        if (has_status_detail && strcmp(state_cache.msg, prompt_body) != 0)
            buddy_app_prompt_detail_append(prompt_detail, sizeof(prompt_detail), state_cache.msg);
    }
    else
    {
        strlcpy(prompt_title,
                ui_text(state_cache.has_state ? UI_TEXT_READY : UI_TEXT_WAITING),
                sizeof(prompt_title));
        strlcpy(prompt_body,
                ui_text(transport.connected ? UI_TEXT_NO_PENDING_REQUEST : UI_TEXT_PAIR_IN_CLAUDE),
                sizeof(prompt_body));
        prompt_detail[0] = '\0';
    }

    snprintf(home_activity_text,
             sizeof(home_activity_text),
             ui_text(UI_TEXT_HOME_ACTIVITY_FMT),
             ui_text(UI_TEXT_NO_RECENT_EVENTS));
    {
        buddy_app_activity_entry_t newest;

        if (buddy_app_activity_snapshot_newest(app, &newest, 1) == 1)
        {
            char activity_summary[96];

            snprintf(activity_summary,
                     sizeof(activity_summary),
                     ui_text(UI_TEXT_HOME_ACTIVITY_FMT),
                     buddy_app_ui_activity_event_text(&newest));
            strlcpy(home_activity_text, activity_summary, sizeof(home_activity_text));
        }
    }
    if (current_locale != applied_locale)
    {
        size_t app_count = 0;
        const ui_app_descriptor_t *apps = ui_app_registry_get(&app_count);

        buddy_app_apply_locale_fonts();
        for (size_t i = 0; i < app_count; ++i)
        {
            if (apps[i].on_locale_changed != NULL)
                apps[i].on_locale_changed((void *)&apps[i]);
        }
        applied_locale = current_locale;
    }
    {
        const ui_app_descriptor_t *active_app =
            ui_app_registry_find_by_page((uint8_t)buddy_app_ui_navigation_current());

        if (active_app != NULL &&
            !buddy_app_descriptor_available(active_app, s_apps_available_caps))
            buddy_app_ui_navigation_go(BUDDY_APP_PAGE_HOME);
    }
    buddy_app_apply_page(buddy_app_ui_navigation_current());
    buddy_app_refresh_home(&state_cache,
                           home_buddy_has_snapshot,
                           title_text,
                           home_date_text,
                           transport_text,
                           buddy_status_text,
                           transport_color,
                           tokens_text,
                           context_compact_text,
                           home_activity_text);
    if (buddy_app_ui_buddy_root() != NULL)
    {
        buddy_app_refresh_buddy(&state_cache,
                                buddy_status_text,
                                transport_color,
                                tokens_text,
                                context_text,
                                context_compact_text);
    }

    buddy_app_refresh_activity(app);
    if (buddy_app_ui_diagnostics_root() != NULL)
        buddy_app_refresh_diagnostics(app);

    buddy_app_refresh_prompt(passkey_active,
                             prompt_active,
                             prompt_title,
                             prompt_body,
                             prompt_detail,
                             prompt_changed);

    if (buddy_app_ui_settings_pack_selector_exists())
    {
        buddy_app_ui_settings_set_pack_detail(pack_detail);
        buddy_app_pack_selector_update(snapshot);
    }
    if (buddy_app_ui_buddy_root() != NULL)
    {
        if (buddy_app_ui_navigation_current() == BUDDY_APP_PAGE_PACK && !s_aod_active)
        {
            buddy_app_ui_buddy_update_gif(have_active, &active_pack, &state_cache, &transport);
        }
    }

    buddy_app_ui_settings_refresh(snapshot,
                                  s_aod_active || s_prompt_forced_page ||
                                  esp_timer_get_time() < s_touch_blocked_until_us);
    buddy_app_refresh_aod(aod_time_text, clock_second_text, home_date_text,
                          transport.connected ? buddy_app_localized_text("BLE connected", "蓝牙已连接") :
                                                buddy_app_localized_text("BLE offline", "蓝牙离线"));
    buddy_app_ui_prompt_set_action_state(prompt_active, snapshot->prompt_reply_submitted);
    buddy_app_ui_prompt_set_attention_anim(attention_active);
    bsp_status_led_mode_t led_mode;

    if (attention_active)
    {
        led_mode = BSP_STATUS_LED_MODE_ATTENTION;
    }
    else if (aod_active)
    {
        led_mode = BSP_STATUS_LED_MODE_OFF;
    }
    else
    {
        led_mode = BSP_STATUS_LED_MODE_NORMAL;
    }
    if (led_mode != s_last_led_mode)
    {
        bsp_status_led_set_mode(led_mode);
        s_last_led_mode = led_mode;
    }

    buddy_app_ui_buddy_sample_gif_progress(&s_gif_progress);
}

static const char *buddy_app_display_diag_phase_name(bsp_display_diag_phase_t phase)
{
    switch (phase)
    {
    case BSP_DISPLAY_DIAG_PHASE_IDLE:
        return "idle";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_START:
        return "flush_start";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_FINISH:
        return "flush_finish";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_START:
        return "wait_start";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_FINISH:
        return "wait_finish";
    default:
        return "unknown";
    }
}

static const char *buddy_app_display_spi_owner_name(bsp_display_spi_owner_t owner)
{
    switch (owner)
    {
    case BSP_DISPLAY_SPI_OWNER_NONE:
        return "none";
    case BSP_DISPLAY_SPI_OWNER_LCD:
        return "lcd";
    case BSP_DISPLAY_SPI_OWNER_TOUCH:
        return "touch";
    case BSP_DISPLAY_SPI_OWNER_ROTATION:
        return "rotation";
    default:
        return "unknown";
    }
}

static void buddy_app_ui_task(void *arg)
{
    buddy_app_t *app = (buddy_app_t *)arg;
    static EXT_RAM_BSS_ATTR buddy_app_ui_snapshot_t snapshot;
    bool have_cached_snapshot = false;
    bool initial_frame_shown = false;
    uint32_t pending_events = UINT32_MAX;
    uint32_t lock_timeout_count = 0;

    while (true)
    {
        /* Do not hold the LVGL lock while waiting for app->mutex. On a
         * transient snapshot failure, render the last complete snapshot. */
        if (buddy_app_ui_snapshot_get(app, &snapshot))
        {
            have_cached_snapshot = true;
        }

        if (bsp_display_lock(BUDDY_APP_UI_LOCK_TIMEOUT_MS))
        {
            if (have_cached_snapshot)
            {
                const ui_app_descriptor_t *descriptor =
                    ui_app_registry_find_by_page((uint8_t)buddy_app_ui_navigation_current());

                if (descriptor != NULL && descriptor->on_event != NULL)
                    descriptor->on_event((void *)descriptor, pending_events);
                buddy_app_ui_refresh(app, &snapshot);
                if (!initial_frame_shown) {
                    lv_refr_now(s_display);
                    if (bsp_display_wait_flush_locked(BUDDY_APP_UI_LOCK_TIMEOUT_MS)) {
                        esp_err_t err = bsp_display_set_brightness(s_aod_active ?
                            snapshot.aod_brightness_percent : snapshot.display_brightness_percent);
                        if (err == ESP_OK) {
                            initial_frame_shown = true;
                            s_aod_brightness_applied = s_aod_active;
                        }
                        else ESP_LOGE("buddy_app_ui", "initial brightness failed: %s", esp_err_to_name(err));
                    } else {
                        ESP_LOGE("buddy_app_ui", "initial display flush timed out");
                    }
                }
                pending_events = 0;
            }
            bsp_display_unlock();
            lock_timeout_count = 0;
        }
        else
        {
            TaskHandle_t state_lock_holder = xSemaphoreGetMutexHolder(app->mutex);

            lock_timeout_count++;
            if (lock_timeout_count == 1U || (lock_timeout_count % 10U) == 0U)
            {
                bsp_display_diag_t display_diag = {0};
                const bool have_display_diag =
                    bsp_display_get_diag(&display_diag) == ESP_OK;
                const int64_t now_us = esp_timer_get_time();
                const int64_t flush_age_ms =
                    have_display_diag && display_diag.flush_phase_time_us > 0
                    ? (now_us - display_diag.flush_phase_time_us) / 1000LL : -1LL;
                const int64_t color_done_age_ms =
                    have_display_diag && display_diag.color_done_time_us > 0
                    ? (now_us - display_diag.color_done_time_us) / 1000LL : -1LL;
                const int64_t touch_age_ms =
                    have_display_diag && display_diag.touch_enter_time_us > 0
                    ? (now_us - display_diag.touch_enter_time_us) / 1000LL : -1LL;
                const int64_t touch_sample_age_ms =
                    have_display_diag && display_diag.touch_sample_enter_time_us > 0
                    ? (now_us - display_diag.touch_sample_enter_time_us) / 1000LL : -1LL;
                const int64_t wrapper_lock_age_ms =
                    have_display_diag && display_diag.wrapper_lock_since_us > 0
                    ? (now_us - display_diag.wrapper_lock_since_us) / 1000LL : -1LL;
                const int64_t spi_gate_owner_age_ms =
                    have_display_diag && display_diag.spi_gate_owner_since_us > 0
                    ? (now_us - display_diag.spi_gate_owner_since_us) / 1000LL : -1LL;
                const int64_t gif_sample_age_ms = s_gif_progress.sample_time_us > 0
                    ? (now_us - s_gif_progress.sample_time_us) / 1000LL : -1LL;
                ESP_LOGE("buddy_app_ui",
                         "LVGL lock timeout count=%lu page=%u app_mutex_holder=%s "
                         "bsp_lock_holder=%s core=%d depth=%lu held=%" PRId64 "ms",
                         (unsigned long)lock_timeout_count,
                         (unsigned)buddy_app_ui_navigation_current(),
                         state_lock_holder != NULL ? pcTaskGetName(state_lock_holder) : "none",
                         have_display_diag && display_diag.wrapper_lock_depth > 0U
                         ? display_diag.wrapper_lock_holder_name : "none",
                         have_display_diag ? display_diag.wrapper_lock_holder_core_id : -1,
                         have_display_diag
                         ? (unsigned long)display_diag.wrapper_lock_depth : 0UL,
                         wrapper_lock_age_ms);
                ESP_LOGE("buddy_app_ui",
                         "display_diag flush_phase=%s age=%" PRId64 "ms "
                         "flush=%lu/%lu wait=%lu/%lu color_done=%lu age=%" PRId64 "ms "
                         "touch_cb=%lu/%lu age=%" PRId64 "ms "
                         "touch_sample=%lu/%lu age=%" PRId64 "ms errors=%lu skipped=%lu points=%u "
                         "spi=%s takes=%lu gives=%lu held=%" PRId64 "ms "
                         "gif_frame=%ld gif_age=%" PRId64 "ms running=%d loaded=%d",
                         have_display_diag
                         ? buddy_app_display_diag_phase_name(display_diag.flush_phase) : "unavailable",
                         flush_age_ms,
                         have_display_diag ? (unsigned long)display_diag.flush_start_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.flush_finish_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.flush_wait_start_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.flush_wait_finish_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.color_done_count : 0UL,
                         color_done_age_ms,
                         have_display_diag ? (unsigned long)display_diag.touch_enter_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.touch_exit_count : 0UL,
                         touch_age_ms,
                         have_display_diag ? (unsigned long)display_diag.touch_sample_enter_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.touch_sample_exit_count : 0UL,
                         touch_sample_age_ms,
                         have_display_diag ? (unsigned long)display_diag.touch_sample_error_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.touch_sample_skip_count : 0UL,
                         have_display_diag ? (unsigned)display_diag.touch_last_point_count : 0U,
                         have_display_diag
                         ? buddy_app_display_spi_owner_name(display_diag.spi_gate_owner) : "unavailable",
                         have_display_diag ? (unsigned long)display_diag.spi_gate_take_count : 0UL,
                         have_display_diag ? (unsigned long)display_diag.spi_gate_give_count : 0UL,
                         spi_gate_owner_age_ms,
                         (long)s_gif_progress.frame_index,
                         gif_sample_age_ms,
                         s_gif_progress.running,
                         s_gif_progress.loaded);
                ESP_LOGE("buddy_app_ui", "gif src=%s frame=%ld/%ld size=%lux%lu",
                         s_gif_progress.source[0] != '\0' ? s_gif_progress.source : "none",
                         (long)s_gif_progress.frame_index,
                         (long)s_gif_progress.frame_count,
                         (unsigned long)s_gif_progress.source_width,
                         (unsigned long)s_gif_progress.source_height);
            }
        }
        if (s_pack_list_refresh_requested)
        {
            s_pack_list_refresh_requested = false;
            (void)buddy_app_charpack_refresh_async(app);
        }
        buddy_app_prompt_reply_process(app);
        pending_events |= buddy_app_ui_wait_for_events(pdMS_TO_TICKS(lock_timeout_count == 0U
            ? BUDDY_APP_UI_CLOCK_REFRESH_MS : BUDDY_APP_UI_LOCK_RETRY_MS));
    }
}

static bool buddy_app_settings_interaction_allowed(void)
{
    bool reset_pending;

    if (s_ui_app == NULL || esp_timer_get_time() < s_touch_blocked_until_us ||
        s_aod_active || s_prompt_forced_page)
    {
        return false;
    }
    if (xSemaphoreTake(s_ui_app->mutex,
                       pdMS_TO_TICKS(BUDDY_APP_UI_STATE_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        return false;
    }
    reset_pending = s_ui_app->settings_reset_pending;
    xSemaphoreGive(s_ui_app->mutex);
    if (reset_pending)
    {
        return false;
    }
    buddy_app_note_activity();
    return true;
}

static void buddy_app_settings_input_activity(void *context)
{
    (void)context;
    if (!s_aod_active && !s_prompt_forced_page && esp_timer_get_time() >= s_touch_blocked_until_us)
        buddy_app_note_activity();
}

static bool buddy_app_settings_allowed_callback(void *context)
{
    (void)context;
    return buddy_app_settings_interaction_allowed();
}

static bool buddy_app_settings_tap_is_click_callback(lv_event_t *event, void *context)
{
    (void)context;
    return buddy_app_tap_event_is_click(event);
}

static const char *buddy_app_localized_text(const char *en, const char *zh)
{
    return ui_locale_get() == UI_LOCALE_ZH_CN ? zh : en;
}

static const char *buddy_app_localized_text_callback(const char *en, const char *zh,
                                                      void *context)
{
    (void)context;
    return buddy_app_localized_text(en, zh);
}

static void buddy_app_settings_request_theme(buddy_app_theme_t theme, void *context)
{
    buddy_app_t *app = context;
    if (app == NULL || theme >= BUDDY_APP_THEME_COUNT ||
        xSemaphoreTake(app->mutex, pdMS_TO_TICKS(BUDDY_APP_UI_STATE_LOCK_TIMEOUT_MS)) != pdTRUE) return;
    app->theme = theme;
    xSemaphoreGive(app->mutex);
    buddy_app_settings_request_save(app);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_SETTINGS);
}

static void buddy_app_settings_request_rotation(buddy_app_rotation_mode_t mode, void *context)
{
    buddy_app_t *app = context;

    if (app == NULL || mode >= BUDDY_APP_ROTATION_MODE_COUNT ||
        xSemaphoreTake(app->mutex, pdMS_TO_TICKS(BUDDY_APP_UI_STATE_LOCK_TIMEOUT_MS)) != pdTRUE)
        return;
    app->rotation_mode = mode;
    xSemaphoreGive(app->mutex);
    buddy_app_settings_request_save(app);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_SETTINGS | BUDDY_APP_UI_DIRTY_LAYOUT);
}

static void buddy_app_settings_language_changed(void *context)
{
    buddy_app_t *app = context;

    /* 选择器已应用目标语言；这里只刷新界面，不能再次反转语言。 */
    if (app == NULL) return;
    buddy_app_apply_locale_fonts();
    buddy_app_settings_request_save(app);
    buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_SETTINGS | BUDDY_APP_UI_DIRTY_TIME);
}

static void buddy_app_settings_set_brightness(uint8_t percent, void *context)
{
    (void)context;
    (void)bsp_display_set_brightness(percent);
}


static esp_err_t buddy_app_settings_request_reset_callback(void *context)
{
    return buddy_app_settings_request_reset(context);
}

static void buddy_app_settings_save_callback(void *context)
{
    buddy_app_settings_request_save(context);
}

static void buddy_app_settings_notify_callback(uint32_t dirty, void *context)
{
    buddy_app_ui_notify(context, dirty);
}

static void buddy_app_settings_open_diagnostics(void *context)
{
    (void)context;
    if (buddy_app_settings_interaction_allowed()) buddy_app_open_registered_app(UI_APP_ID_DIAGNOSTICS);
}

static void buddy_app_diagnostics_back(lv_event_t *event)
{
    /* 复用设置的交互检查，保留审批和 AOD 唤醒期间的操作限制。 */
    if (buddy_app_settings_tap_is_click_callback(event, NULL) && buddy_app_settings_interaction_allowed())
        buddy_app_navigate_to(BUDDY_APP_PAGE_SETTINGS);
}

static bool buddy_app_settings_create_menu(lv_obj_t *parent)
{
    const buddy_app_ui_settings_callbacks_t callbacks = {
        .track_tap_event = buddy_app_track_tap_event,
        .pack_selector_event = buddy_app_pack_selector_event,
        .interaction_allowed = buddy_app_settings_allowed_callback,
        .input_activity = buddy_app_settings_input_activity,
        .tap_is_click = buddy_app_settings_tap_is_click_callback,
        .localized_text = buddy_app_localized_text_callback,
        .request_theme = buddy_app_settings_request_theme,
        .request_rotation = buddy_app_settings_request_rotation,
        .open_diagnostics = buddy_app_settings_open_diagnostics,
        .language_changed = buddy_app_settings_language_changed,
        .set_brightness = buddy_app_settings_set_brightness,
        .request_reset = buddy_app_settings_request_reset_callback,
        .request_save = buddy_app_settings_save_callback,
        .notify = buddy_app_settings_notify_callback,
        .context = s_ui_app,
    };
    return buddy_app_ui_settings_create(parent, s_ui_app, &callbacks);
}


static void buddy_app_registry_on_show(void *context)
{
    const ui_app_descriptor_t *descriptor = context;

    if (descriptor == NULL)
        return;
    if (descriptor->id == UI_APP_ID_BUDDY)
        buddy_app_ui_buddy_set_gif_playing(true);

}

static void buddy_app_registry_on_hide(void *context)
{
    const ui_app_descriptor_t *descriptor = context;
    lv_obj_t *page;

    if (descriptor == NULL)
        return;
    page = buddy_app_page_object((buddy_app_ui_page_t)descriptor->page);
    if (descriptor->id == UI_APP_ID_BUDDY)
        buddy_app_ui_buddy_set_gif_playing(false);
    if (page != NULL)
        lv_anim_delete(page, NULL);
}

static void buddy_app_registry_on_event(void *context, uint32_t event)
{
    const ui_app_descriptor_t *descriptor = context;

    if (descriptor == NULL)
        return;
    if (descriptor->id == UI_APP_ID_BUDDY && (event & BUDDY_APP_UI_DIRTY_BUDDY) != 0 &&
        buddy_app_ui_settings_pack_selector_exists())
        buddy_app_ui_settings_invalidate_pack_selector();
}

static void buddy_app_registry_on_locale_changed(void *context)
{
    const ui_app_descriptor_t *descriptor = context;
    lv_obj_t *page;

    if (descriptor == NULL)
        return;
    page = buddy_app_page_object((buddy_app_ui_page_t)descriptor->page);
    if (page != NULL)
    {
        lv_obj_invalidate(page);
    }
}

esp_err_t buddy_app_ui_core_init(buddy_app_t *app)
{
    lv_obj_t *scr;
    buddy_app_theme_t initial_theme;
    if (xSemaphoreTake(app->mutex, pdMS_TO_TICKS(BUDDY_APP_UI_STATE_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
    initial_theme = app->theme;
    xSemaphoreGive(app->mutex);

    s_display = bsp_display_start();
    ESP_RETURN_ON_FALSE(s_display != NULL, ESP_FAIL, "buddy_app_ui", "display start");
    ESP_RETURN_ON_ERROR(bsp_display_set_brightness(0),
                        "buddy_app_ui", "keep startup backlight off");
    if (bsp_status_led_start() != ESP_OK)
    {
        ESP_LOGW("buddy_app_ui", "status LED unavailable");
    }
    s_ui_app = app;

    if (!bsp_display_lock(BUDDY_APP_UI_LOCK_TIMEOUT_MS))
    {
        return ESP_FAIL;
    }

    ui_locale_font_init();
    esp_err_t theme_err = buddy_ui_theme_init(s_display, initial_theme);
    if (theme_err != ESP_OK) {
        bsp_display_unlock();
        return theme_err;
    }

    scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BUDDY_APP_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(BUDDY_APP_COLOR_TEXT), 0);

    buddy_app_ui_diagnostics_set_navigation(buddy_app_diagnostics_back, buddy_app_track_tap_event);
    if (!ui_app_registry_bind(UI_APP_ID_BUDDY, BUDDY_APP_PAGE_PACK,
                              buddy_app_ui_buddy_root_slot(), buddy_app_ui_buddy_create,
                              buddy_app_registry_on_show, buddy_app_registry_on_hide,
                              buddy_app_registry_on_event, buddy_app_registry_on_locale_changed) ||
        !ui_app_registry_bind(UI_APP_ID_DIAGNOSTICS, BUDDY_APP_PAGE_DIAGNOSTICS,
                              buddy_app_ui_diagnostics_root_slot(),
                              buddy_app_ui_diagnostics_create,
                              buddy_app_registry_on_show, buddy_app_registry_on_hide,
                              buddy_app_registry_on_event, buddy_app_registry_on_locale_changed))
    {
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (!buddy_app_ui_activity_create(scr)) {
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    {
        const buddy_app_ui_home_callbacks_t callbacks = {
            .app_card_event = buddy_app_app_card_event,
            .track_tap_event = buddy_app_track_tap_event,
            .action_event = buddy_app_action_event,
            .activity_action = BUDDY_ACTION_ACTIVITY,
        };

        if (!buddy_app_ui_home_create(scr, &callbacks)) {
            bsp_display_unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }

    {
        const buddy_app_ui_prompt_callbacks_t callbacks = {
            .track_tap_event = buddy_app_track_tap_event,
            .action_event = buddy_app_action_event,
            .allow_action = BUDDY_ACTION_ALLOW,
            .deny_action = BUDDY_ACTION_DENY,
        };

        if (!buddy_app_ui_prompt_create(scr, &callbacks)) {
            bsp_display_unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (!buddy_app_settings_create_menu(scr)) {
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    {
        const buddy_app_ui_aod_callbacks_t callbacks = {
            .action_event = buddy_app_action_event,
            .home_action = BUDDY_ACTION_HOME,
        };

        if (!buddy_app_ui_aod_create(scr, &callbacks)) {
            bsp_display_unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }

    buddy_app_ui_status_bar_create(scr);

    s_nav = lv_obj_create(scr);
    lv_obj_remove_style_all(s_nav);
    lv_obj_set_size(s_nav, LV_PCT(100), 48);
    lv_obj_align(s_nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(s_nav, lv_color_hex(BUDDY_APP_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(s_nav, LV_OPA_COVER, 0);
    buddy_app_create_touch_button(s_nav, LV_SYMBOL_HOME, BUDDY_ACTION_HOME);
    buddy_app_create_touch_button(s_nav, LV_SYMBOL_BLUETOOTH, BUDDY_ACTION_BUDDY);
    buddy_app_create_touch_button(s_nav, LV_SYMBOL_BELL, BUDDY_ACTION_ACTIVITY);
    buddy_app_create_touch_button(s_nav, LV_SYMBOL_SETTINGS, BUDDY_ACTION_SETTINGS);
    buddy_app_apply_locale_fonts();
    buddy_app_relayout();

    buddy_app_ui_buddy_reset_gif();
    s_gif_progress.frame_index = -1;
    s_gif_progress.frame_count = -1;
    buddy_app_ui_navigation_init();
    s_prompt_forced_page = false;
    s_aod_active = false;
    s_last_activity_us = esp_timer_get_time();
    s_touch_blocked_until_us = s_last_activity_us + BUDDY_APP_UI_START_TOUCH_GUARD_US;
    buddy_app_apply_page(buddy_app_ui_navigation_current());
    buddy_app_ui_prompt_set_action_state(false, false);
    bsp_memory_log_stage("base UI created");

    bsp_display_unlock();
    return ESP_OK;
}

void buddy_app_ui_core_start(buddy_app_t *app)
{
    if (xTaskCreatePinnedToCore(buddy_app_ui_task,
                                "buddy_app_ui",
                                BUDDY_APP_UI_STACK,
                                app,
                                BUDDY_APP_UI_PRIORITY,
                                &app->ui_task,
                                BUDDY_APP_UI_CORE) != pdPASS)
    {
        app->ui_task = NULL;
    }
}
