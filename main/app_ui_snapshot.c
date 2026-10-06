/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "app_shared.h"

#define BUDDY_APP_UI_SNAPSHOT_TIMEOUT_MS 20

bool buddy_app_ui_snapshot_get(buddy_app_t *app, buddy_app_ui_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    if (app == NULL || app->mutex == NULL) {
        memset(snapshot, 0, sizeof(*snapshot));
        return false;
    }

    /* This is also called from LVGL callbacks. Never hold the LVGL lock while
     * waiting indefinitely for application state. Keep the previous snapshot
     * when the state is briefly busy. */
    if (xSemaphoreTake(app->mutex, pdMS_TO_TICKS(BUDDY_APP_UI_SNAPSHOT_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    snapshot->available_caps = app->available_caps;
    snapshot->state_cache = app->state_cache;
    snapshot->prompt_reply_submitted = app->state_cache.prompt.present &&
        (strcmp(app->prompt_reply_queued_id, app->state_cache.prompt.id) == 0 ||
         strcmp(app->prompt_reply_inflight_id, app->state_cache.prompt.id) == 0 ||
         strcmp(app->prompt_reply_sent_id, app->state_cache.prompt.id) == 0);
    snapshot->prompt_reply_failed = app->state_cache.prompt.present &&
        strcmp(app->prompt_reply_error_id, app->state_cache.prompt.id) == 0;
    snapshot->transport_state = app->transport_state;
    snapshot->active_pack = app->active_pack;
    snapshot->have_active_pack = app->have_active_pack;
    strlcpy(snapshot->pack_status, app->pack_status, sizeof(snapshot->pack_status));
    memcpy(snapshot->installed_packs,
           app->installed_packs,
           sizeof(snapshot->installed_packs));
    snapshot->installed_pack_count = app->installed_pack_count;
    snapshot->charpack_list_pending = app->charpack_list_pending;
    snapshot->charpack_list_result = app->charpack_list_result;
    snapshot->charpack_switch_pending = app->charpack_switch_pending;
    snapshot->charpack_switch_result = app->charpack_switch_result;
    strlcpy(snapshot->advertising_name,
            app->advertising_name,
            sizeof(snapshot->advertising_name));
    snapshot->aod_timeout_minutes = app->aod_timeout_minutes;
    snapshot->aod_enabled = app->aod_enabled;
    snapshot->settings_reset_pending = app->settings_reset_pending;
    snapshot->settings_reset_result = app->settings_reset_result;
    snapshot->settings_migration_result = app->settings_migration_result;
    snapshot->display_brightness_percent = app->display_brightness_percent;
    snapshot->aod_brightness_percent = app->aod_brightness_percent;
    snapshot->rotation_mode = app->rotation_mode;
    snapshot->theme = app->theme;
    strlcpy(snapshot->ble_device_name, app->ble_device_name, sizeof(snapshot->ble_device_name));
    snapshot->device_names_pending = app->device_names_pending;
    snapshot->device_names_result = app->device_names_result;
    snapshot->time_source = app->time_source;
    snapshot->time_last_synced_seconds = app->time_last_synced_seconds;
    snapshot->tz_offset_seconds = app->tz_offset_seconds;
    xSemaphoreGive(app->mutex);
    return true;
}
