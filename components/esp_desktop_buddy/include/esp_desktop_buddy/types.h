/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_DESKTOP_BUDDY_LINE_MAX CONFIG_ESP_DESKTOP_BUDDY_LINE_MAX
#define ESP_DESKTOP_BUDDY_FRAME_MAX (ESP_DESKTOP_BUDDY_LINE_MAX + 1)
#define ESP_DESKTOP_BUDDY_TURN_ROLE_MAX 16

/**
 * @brief Borrowed JSON byte view used by the public Buddy API.
 *
 * The underlying bytes are owned by the producer. Unless a specific API says
 * otherwise, callers must not retain this view beyond the call or callback
 * that supplied it.
 */
typedef struct {
    const uint8_t *bytes;
    size_t len;
} esp_desktop_buddy_json_view_t;

/** Borrowed JSON-object byte view used by status handlers. */
typedef esp_desktop_buddy_json_view_t esp_desktop_buddy_json_object_view_t;

/** Public scalar state mirrored by the Buddy core. */
typedef struct {
    bool has_state;
    bool live;
    bool prompt_present;
    bool prompt_hint_truncated;
    uint32_t total;
    uint32_t running;
    uint32_t waiting;
    uint64_t tokens;
    uint64_t tokens_today;
    bool context_value_valid;
    bool context_window_valid;
    uint64_t context_value;
    uint64_t context_window;
} esp_desktop_buddy_snapshot_info_t;

/** Parsed time synchronization payload emitted by the core. `tz_offset` is UTC offset in seconds. */
typedef struct {
    int64_t epoch;
    int32_t tz_offset;
} esp_desktop_buddy_time_sync_t;

/* Unix 秒限定 2020-01-01 至 2099-12-31；UTC 偏移以东区为正，支持 15 分钟粒度。 */
#define ESP_DESKTOP_BUDDY_TIME_EPOCH_MIN 1577836800LL
#define ESP_DESKTOP_BUDDY_TIME_EPOCH_MAX 4102444799LL
#define ESP_DESKTOP_BUDDY_TIME_OFFSET_MIN (-43200)
#define ESP_DESKTOP_BUDDY_TIME_OFFSET_MAX 50400
#define ESP_DESKTOP_BUDDY_TIME_OFFSET_STEP 900

static inline bool esp_desktop_buddy_time_sync_valid(const esp_desktop_buddy_time_sync_t *sync)
{
    return sync != NULL && sync->epoch >= ESP_DESKTOP_BUDDY_TIME_EPOCH_MIN &&
           sync->epoch <= ESP_DESKTOP_BUDDY_TIME_EPOCH_MAX &&
           sync->tz_offset >= ESP_DESKTOP_BUDDY_TIME_OFFSET_MIN &&
           sync->tz_offset <= ESP_DESKTOP_BUDDY_TIME_OFFSET_MAX &&
           sync->tz_offset % ESP_DESKTOP_BUDDY_TIME_OFFSET_STEP == 0;
}


/**
 * @brief Parsed Claude turn payload emitted by the core.
 *
 * `content` contains the serialized JSON value of the wire content field. This
 * may be a JSON string or a JSON array of content blocks. For
 * `ESP_DESKTOP_BUDDY_EVENT_TURN`, content is borrowed and remains valid only until the
 * event callback returns.
 */
typedef struct {
    char role[ESP_DESKTOP_BUDDY_TURN_ROLE_MAX + 1];
    esp_desktop_buddy_json_view_t content;
    uint32_t line_len;
} esp_desktop_buddy_turn_t;

#ifdef __cplusplus
}
#endif
