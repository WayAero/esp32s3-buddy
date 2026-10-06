/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "permission.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Typed events emitted by the Buddy core. */
typedef enum {
    ESP_DESKTOP_BUDDY_EVENT_SNAPSHOT_UPDATED = 0,
    ESP_DESKTOP_BUDDY_EVENT_PERMISSION_SENT,
    ESP_DESKTOP_BUDDY_EVENT_TIME_SYNC,
    ESP_DESKTOP_BUDDY_EVENT_TURN,
    ESP_DESKTOP_BUDDY_EVENT_LIVENESS_CHANGED,
    ESP_DESKTOP_BUDDY_EVENT_ERROR,
} esp_desktop_buddy_event_type_t;

/** High-level error classes surfaced through ESP_DESKTOP_BUDDY_EVENT_ERROR. */
typedef enum {
    ESP_DESKTOP_BUDDY_ERROR_INPUT = 0,
    ESP_DESKTOP_BUDDY_ERROR_BACKPRESSURE,
    ESP_DESKTOP_BUDDY_ERROR_APP,
    ESP_DESKTOP_BUDDY_ERROR_ACTION,
    ESP_DESKTOP_BUDDY_ERROR_INTERNAL,
} esp_desktop_buddy_error_kind_t;

/** Payload for emitted permission-sent events. */
typedef struct {
    /* Borrowed prompt id valid only until the callback returns. */
    const char *prompt_id;
    esp_desktop_buddy_permission_decision_t decision;
} esp_desktop_buddy_permission_sent_t;

/** Payload for emitted error events. */
typedef struct {
    esp_desktop_buddy_error_kind_t kind;
    uint32_t detail;
} esp_desktop_buddy_error_event_t;

/**
 * @brief Event union delivered to the application event sink.
 *
 * Fields that contain borrowed views, such as `esp_desktop_buddy_turn_t.content` and
 * `esp_desktop_buddy_permission_sent_t.prompt_id`, are valid only for the duration of the
 * callback that receives the event.
 */
typedef struct {
    esp_desktop_buddy_event_type_t type;
    union {
        esp_desktop_buddy_permission_sent_t permission_sent;
        esp_desktop_buddy_time_sync_t time_sync;
        esp_desktop_buddy_turn_t turn;
        bool live;
        esp_desktop_buddy_error_event_t error;
    } data;
} esp_desktop_buddy_event_t;

/**
 * @brief Callback invoked synchronously on the Buddy core task when the core emits a typed event.
 *
 * The core currently exposes a single event sink. Applications that need
 * fan-out should forward or multiplex events from this callback onto their own
 * event loop or task boundary.
 */
typedef void (*esp_desktop_buddy_event_handler_t)(void *ctx, const esp_desktop_buddy_event_t *event);

/** Event sink used by the core to deliver typed Buddy events. */
typedef struct {
    esp_desktop_buddy_event_handler_t on_event;
    void *ctx;
} esp_desktop_buddy_event_listener_t;

#ifdef __cplusplus
}
#endif
