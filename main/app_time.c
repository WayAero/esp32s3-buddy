/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "app_shared.h"

/* 校时串行化后再取得应用锁。设置系统时间及提交状态在同一临界区，
 * Activity 不会记录新时间和旧偏移的组合；期限仍使用单调时钟。 */
esp_err_t buddy_app_time_apply_ble_sync(buddy_app_t *app,
                                        const esp_desktop_buddy_time_sync_t *sync)
{
    if (app == NULL || app->mutex == NULL || app->time_sync_mutex == NULL ||
        !esp_desktop_buddy_time_sync_valid(sync)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(app->time_sync_mutex, portMAX_DELAY);
    xSemaphoreTake(app->mutex, portMAX_DELAY);
    bool secure = app->transport_state.connected && app->transport_state.encrypted &&
                  app->transport_state.subscribed;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (secure) {
        const struct timeval tv = {.tv_sec = (time_t)sync->epoch, .tv_usec = 0};
        result = settimeofday(&tv, NULL) == 0 ? ESP_OK : ESP_FAIL;
        if (result == ESP_OK) {
            app->tz_offset_seconds = sync->tz_offset;
            app->time_source = BUDDY_APP_TIME_BLE_SYNCED;
            app->time_last_synced_seconds = sync->epoch;
        }
    }
    xSemaphoreGive(app->mutex);
    xSemaphoreGive(app->time_sync_mutex);
    if (result == ESP_OK) {
        buddy_app_ui_notify(app, BUDDY_APP_UI_DIRTY_TIME | BUDDY_APP_UI_DIRTY_SETTINGS);
    }
    return result;
}
