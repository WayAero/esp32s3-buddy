/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_desktop_buddy/core.h"

#define BUDDY_CORE_FEED_RX_CHUNK_MAX CONFIG_ESP_DESKTOP_BUDDY_FEED_RX_CHUNK_MAX
#define BUDDY_RX_INBOX_DEPTH CONFIG_ESP_DESKTOP_BUDDY_RX_INBOX_DEPTH
#define BUDDY_ACTION_QUEUE_DEPTH CONFIG_ESP_DESKTOP_BUDDY_ACTION_QUEUE_DEPTH
#define BUDDY_TX_QUEUE_DEPTH CONFIG_ESP_DESKTOP_BUDDY_TX_QUEUE_DEPTH
#define BUDDY_LIVE_TIMEOUT_MS CONFIG_ESP_DESKTOP_BUDDY_LIVE_TIMEOUT_MS
#define BUDDY_LOG_PREVIEW_LEN CONFIG_ESP_DESKTOP_BUDDY_LOG_PREVIEW_LEN
#define BUDDY_INBOX_DEPTH (BUDDY_RX_INBOX_DEPTH + BUDDY_ACTION_QUEUE_DEPTH)
#define BUDDY_TURN_SERIALIZED_LIMIT 4096
#define BUDDY_CORE_TASK_STACK 12288
#define BUDDY_CORE_TASK_PRIORITY 5
#define BUDDY_CORE_TASK_CORE_ID 0
#define BUDDY_STATE_ENTRY_COUNT_MAX CONFIG_ESP_DESKTOP_BUDDY_ENTRY_COUNT_MAX
#define BUDDY_STATE_ENTRY_STRING_MAX CONFIG_ESP_DESKTOP_BUDDY_ENTRY_STRING_MAX
#define BUDDY_STATE_MESSAGE_MAX CONFIG_ESP_DESKTOP_BUDDY_MESSAGE_MAX
#define BUDDY_STATE_PROMPT_ID_MAX CONFIG_ESP_DESKTOP_BUDDY_PROMPT_ID_MAX
#define BUDDY_STATE_PROMPT_TOOL_MAX CONFIG_ESP_DESKTOP_BUDDY_PROMPT_TOOL_MAX
#define BUDDY_STATE_PROMPT_HINT_MAX CONFIG_ESP_DESKTOP_BUDDY_PROMPT_HINT_MAX

#define BUDDY_TOKEN_INVALID_REQUEST "invalid_request"
#define BUDDY_TOKEN_UNSUPPORTED "unsupported"
#define BUDDY_TOKEN_REJECTED "rejected"
#define BUDDY_TOKEN_INTERNAL "internal"
#define BUDDY_TOKEN_INVALID_STATUS_DATA "invalid_status_data"
#define BUDDY_TOKEN_STATUS_TOO_LARGE "status_too_large"
#define BUDDY_TOKEN_UNKNOWN_COMMAND "unknown_command"

typedef struct {
    bool present;
    bool hint_truncated;
    char id[BUDDY_STATE_PROMPT_ID_MAX + 1];
    char tool[BUDDY_STATE_PROMPT_TOOL_MAX + 1];
    char hint[BUDDY_STATE_PROMPT_HINT_MAX + 1];
} buddy_prompt_state_t;

typedef struct {
    bool has_state;
    uint32_t total;
    uint32_t running;
    uint32_t waiting;
    uint64_t tokens;
    uint64_t tokens_today;
    bool context_value_valid;
    bool context_window_valid;
    uint64_t context_value;
    uint64_t context_window;
    char msg[BUDDY_STATE_MESSAGE_MAX + 1];
    char entries[BUDDY_STATE_ENTRY_COUNT_MAX][BUDDY_STATE_ENTRY_STRING_MAX + 1];
    size_t entry_count;
    buddy_prompt_state_t prompt;
} buddy_state_t;

typedef struct {
    esp_desktop_buddy_permission_reply_type_t type;
    char prompt_id[BUDDY_STATE_PROMPT_ID_MAX + 1];
} buddy_action_item_t;

typedef enum {
    BUDDY_INBOX_ITEM_RX = 0,
    BUDDY_INBOX_ITEM_ACTION,
    BUDDY_INBOX_ITEM_STOP,
} buddy_inbox_item_type_t;

typedef struct {
    buddy_inbox_item_type_t type;
    union {
        struct {
            size_t len;
            int64_t enqueued_us;
            uint8_t bytes[BUDDY_CORE_FEED_RX_CHUNK_MAX];
        } rx;
        buddy_action_item_t action;
    } data;
} buddy_inbox_item_t;

typedef struct {
    size_t len;
    bool is_folder_chunk_ack;
    int64_t enqueued_us;
    uint8_t bytes[ESP_DESKTOP_BUDDY_FRAME_MAX];
} esp_desktop_buddy_tx_frame_t;

typedef struct {
    char buf[ESP_DESKTOP_BUDDY_LINE_MAX + 1];
    size_t len;
    bool dropping;
} buddy_linebuf_t;

struct esp_desktop_buddy {
    QueueHandle_t inbox_queue;
    QueueHandle_t tx_queue;
    SemaphoreHandle_t state_mutex;
    SemaphoreHandle_t stop_sem;
    TaskHandle_t task;
    esp_desktop_buddy_event_listener_t event_sink;
    esp_desktop_buddy_command_handlers_t handlers;
    buddy_linebuf_t linebuf;
    buddy_state_t state;
    int64_t last_state_ms;
    bool live;
    bool prompt_response_sent;
    bool transport_attached;
    esp_desktop_buddy_rx_diagnostics_t rx_diagnostics;
    volatile bool delete_requested;
};

int64_t buddy_now_ms(void);
bool buddy_emit_event(esp_desktop_buddy_t *buddy, const esp_desktop_buddy_event_t *event);
void buddy_emit_error(esp_desktop_buddy_t *buddy, esp_desktop_buddy_error_kind_t kind, uint32_t detail);
bool buddy_state_is_live_at(const esp_desktop_buddy_t *buddy, int64_t now_ms);
void buddy_maybe_emit_liveness_change(esp_desktop_buddy_t *buddy, int64_t now_ms);

bool buddy_copy_utf8_trunc(char *dst, size_t dst_size, const char *src);
esp_err_t buddy_copy_string_out(char *dst,
                                size_t dst_size,
                                const char *src,
                                size_t *out_required_size);
const char *esp_desktop_buddy_command_error_token(esp_err_t err);
const char *esp_desktop_buddy_status_error_token(esp_err_t err);

static inline void buddy_log_line_preview(const char *tag,
                                          const char *prefix,
                                          const char *line,
                                          size_t line_len)
{
    int preview_len = (int)(line_len > BUDDY_LOG_PREVIEW_LEN ? BUDDY_LOG_PREVIEW_LEN : line_len);

    ESP_LOGI(tag,
             "%s len=%u %.*s%s",
             prefix,
             (unsigned)line_len,
             preview_len,
             line,
             line_len > (size_t)preview_len ? "..." : "");
}

static inline void buddy_log_protocol_line(const char *tag,
                                           const char *prefix,
                                           const char *line,
                                           size_t line_len)
{
#if CONFIG_ESP_DESKTOP_BUDDY_PROTOCOL_TRACE
    buddy_log_line_preview(tag, prefix, line, line_len);
#else
    (void)tag;
    (void)prefix;
    (void)line;
    (void)line_len;
#endif
}

esp_err_t esp_desktop_buddy_txq_enqueue_frame(esp_desktop_buddy_t *buddy,
                                              const uint8_t *bytes,
                                              size_t len,
                                              bool is_folder_chunk_ack);
esp_err_t buddy_codec_queue_ack(esp_desktop_buddy_t *buddy,
                                const char *ack,
                                bool ok,
                                uint32_t n,
                                const char *error);
esp_err_t buddy_codec_queue_status_result(esp_desktop_buddy_t *buddy, esp_desktop_buddy_status_reply_t result);
esp_err_t buddy_codec_queue_permission_reply(esp_desktop_buddy_t *buddy,
                                             const char *prompt_id,
                                             esp_desktop_buddy_permission_decision_t decision);

void buddy_linebuf_feed(esp_desktop_buddy_t *buddy, const uint8_t *data, size_t len);
void buddy_protocol_process_line(esp_desktop_buddy_t *buddy, const char *line, size_t line_len);
