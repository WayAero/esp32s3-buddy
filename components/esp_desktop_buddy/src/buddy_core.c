/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include "buddy_internal.h"

static const char *TAG = "buddy_core";

static void buddy_destroy_from_task(esp_desktop_buddy_t *buddy)
{
    QueueHandle_t inbox_queue;
    QueueHandle_t tx_queue;
    SemaphoreHandle_t state_mutex;
    SemaphoreHandle_t stop_sem;

    if (buddy == NULL) {
        vTaskDelete(NULL);
        return;
    }

    inbox_queue = buddy->inbox_queue;
    tx_queue = buddy->tx_queue;
    state_mutex = buddy->state_mutex;
    stop_sem = buddy->stop_sem;

    buddy->inbox_queue = NULL;
    buddy->tx_queue = NULL;
    buddy->state_mutex = NULL;
    buddy->stop_sem = NULL;
    buddy->task = NULL;

    if (inbox_queue != NULL) {
        vQueueDelete(inbox_queue);
    }
    if (tx_queue != NULL) {
        vQueueDelete(tx_queue);
    }
    if (state_mutex != NULL) {
        vSemaphoreDelete(state_mutex);
    }
    if (stop_sem != NULL) {
        vSemaphoreDelete(stop_sem);
    }

    free(buddy);
    vTaskDelete(NULL);
}

static void buddy_core_task(void *arg)
{
    esp_desktop_buddy_t *buddy = (esp_desktop_buddy_t *)arg;
    buddy_inbox_item_t item;

    while (true) {
        TickType_t wait_ticks = pdMS_TO_TICKS(250);
        int64_t now_ms = buddy_now_ms();
        bool live = false;

        xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
        live = buddy->live;
        if (buddy->last_state_ms != 0) {
            int64_t remaining_ms = BUDDY_LIVE_TIMEOUT_MS - (now_ms - buddy->last_state_ms);
            if (remaining_ms <= 0) {
                // Wake immediately once to flip live -> false, then fall back to a
                // normal sleep interval instead of spinning on expired state.
                wait_ticks = live ? 0 : pdMS_TO_TICKS(250);
            } else {
                wait_ticks = pdMS_TO_TICKS((uint32_t)remaining_ms);
                if (wait_ticks == 0) {
                    wait_ticks = 1;
                }
            }
        }
        xSemaphoreGive(buddy->state_mutex);

        if (xQueueReceive(buddy->inbox_queue, &item, wait_ticks) != pdTRUE) {
            buddy_maybe_emit_liveness_change(buddy, buddy_now_ms());
            if (buddy->delete_requested) {
                buddy_destroy_from_task(buddy);
            }
            continue;
        }

        switch (item.type) {
        case BUDDY_INBOX_ITEM_RX:
            if (item.data.rx.enqueued_us > 0) {
                const uint32_t queue_wait_us = (uint32_t)(esp_timer_get_time() - item.data.rx.enqueued_us);

                xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
                buddy->rx_diagnostics.queue_wait_count++;
                buddy->rx_diagnostics.queue_wait_total_us += queue_wait_us;
                if (queue_wait_us > buddy->rx_diagnostics.queue_wait_max_us) {
                    buddy->rx_diagnostics.queue_wait_max_us = queue_wait_us;
                }
                xSemaphoreGive(buddy->state_mutex);
            }
            buddy_linebuf_feed(buddy, item.data.rx.bytes, item.data.rx.len);
            buddy_maybe_emit_liveness_change(buddy, buddy_now_ms());
            break;
        case BUDDY_INBOX_ITEM_ACTION: {
            buddy_state_t state;
            bool can_reply = false;
            int64_t now_ms = buddy_now_ms();
            esp_err_t err;
            esp_desktop_buddy_permission_decision_t decision = ESP_DESKTOP_BUDDY_PERMISSION_DECISION_ONCE;

            buddy_maybe_emit_liveness_change(buddy, now_ms);

            switch (item.data.action.type) {
            case ESP_DESKTOP_BUDDY_PERMISSION_REPLY_DENY:
                decision = ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY;
                break;
            case ESP_DESKTOP_BUDDY_PERMISSION_REPLY_ONCE:
                decision = ESP_DESKTOP_BUDDY_PERMISSION_DECISION_ONCE;
                break;
            default:
                buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_ACTION, 0);
                break;
            }
            if (item.data.action.type != ESP_DESKTOP_BUDDY_PERMISSION_REPLY_DENY &&
                item.data.action.type != ESP_DESKTOP_BUDDY_PERMISSION_REPLY_ONCE) {
                break;
            }

            xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
            state = buddy->state;
            can_reply = buddy_state_is_live_at(buddy, now_ms) &&
                        state.prompt.present &&
                        !buddy->prompt_response_sent &&
                        strcmp(state.prompt.id, item.data.action.prompt_id) == 0;
            if (!can_reply) {
                xSemaphoreGive(buddy->state_mutex);
                ESP_LOGW(TAG, "dropping action for stale or missing prompt id=%s",
                         item.data.action.prompt_id);
                buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_ACTION, 0);
                break;
            }

            /* Keep validation, TX enqueue and the sent marker atomic with
             * transport-session reset. The TX queue operation is non-blocking. */
            err = buddy_codec_queue_permission_reply(buddy, state.prompt.id, decision);
            if (err != ESP_OK) {
                xSemaphoreGive(buddy->state_mutex);
                ESP_LOGW(TAG, "failed to queue permission reply: %s", esp_err_to_name(err));
                buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_ACTION, (uint32_t)err);
                break;
            }
            buddy->prompt_response_sent = true;
            xSemaphoreGive(buddy->state_mutex);

            esp_desktop_buddy_event_t event = {
                .type = ESP_DESKTOP_BUDDY_EVENT_PERMISSION_SENT,
            };
            event.data.permission_sent.prompt_id = state.prompt.id;
            event.data.permission_sent.decision = decision;
            buddy_emit_event(buddy, &event);
            break;
        }
        case BUDDY_INBOX_ITEM_STOP:
            xSemaphoreGive(buddy->stop_sem);
            vTaskDelete(NULL);
            return;
        default:
            buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INTERNAL, 0);
            break;
        }

        if (buddy->delete_requested) {
            buddy_destroy_from_task(buddy);
        }
    }
}

int64_t buddy_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

bool buddy_emit_event(esp_desktop_buddy_t *buddy, const esp_desktop_buddy_event_t *event)
{
    if (buddy == NULL || event == NULL || buddy->event_sink.on_event == NULL) {
        return true;
    }

    buddy->event_sink.on_event(buddy->event_sink.ctx, event);
    return !buddy->delete_requested;
}

void buddy_emit_error(esp_desktop_buddy_t *buddy, esp_desktop_buddy_error_kind_t kind, uint32_t detail)
{
    esp_desktop_buddy_event_t event = {
        .type = ESP_DESKTOP_BUDDY_EVENT_ERROR,
    };
    event.data.error.kind = kind;
    event.data.error.detail = detail;
    buddy_emit_event(buddy, &event);
}

bool buddy_state_is_live_at(const esp_desktop_buddy_t *buddy, int64_t now_ms)
{
    return buddy != NULL &&
           buddy->last_state_ms != 0 &&
           (now_ms - buddy->last_state_ms) <= BUDDY_LIVE_TIMEOUT_MS;
}

void buddy_maybe_emit_liveness_change(esp_desktop_buddy_t *buddy, int64_t now_ms)
{
    bool changed = false;
    bool live = false;

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    live = buddy_state_is_live_at(buddy, now_ms);
    changed = live != buddy->live;
    buddy->live = live;
    xSemaphoreGive(buddy->state_mutex);

    if (changed) {
        esp_desktop_buddy_event_t event = {
            .type = ESP_DESKTOP_BUDDY_EVENT_LIVENESS_CHANGED,
        };
        event.data.live = live;
        buddy_emit_event(buddy, &event);
    }
}

bool buddy_copy_utf8_trunc(char *dst, size_t dst_size, const char *src)
{
    size_t src_len;
    size_t copy_len;

    if (dst == NULL || dst_size == 0) {
        return false;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return false;
    }

    src_len = strlen(src);
    copy_len = src_len;
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1;
    }

    while (copy_len > 0 && (((unsigned char)src[copy_len] & 0xC0u) == 0x80u)) {
        copy_len--;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return copy_len < src_len;
}

esp_err_t buddy_copy_string_out(char *dst,
                                size_t dst_size,
                                const char *src,
                                size_t *out_required_size)
{
    size_t required_size = 1;

    if (src != NULL) {
        required_size = strlen(src) + 1;
    }
    if (out_required_size != NULL) {
        *out_required_size = required_size;
    }
    if (dst == NULL) {
        return dst_size == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (dst_size == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    strlcpy(dst, src != NULL ? src : "", dst_size);
    return dst_size >= required_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_desktop_buddy_command_result_t esp_desktop_buddy_command_ok(void)
{
    esp_desktop_buddy_command_result_t result = {
        .err = ESP_OK,
        .detail = NULL,
    };
    return result;
}

esp_desktop_buddy_command_result_t esp_desktop_buddy_command_err(esp_err_t err, const char *detail)
{
    esp_desktop_buddy_command_result_t result = {
        .err = err,
        .detail = detail,
    };
    return result;
}

esp_desktop_buddy_status_reply_t esp_desktop_buddy_status_ok(esp_desktop_buddy_json_object_view_t data)
{
    esp_desktop_buddy_status_reply_t result = {
        .result = esp_desktop_buddy_command_ok(),
        .data = data,
    };
    return result;
}

esp_desktop_buddy_status_reply_t esp_desktop_buddy_status_err(esp_err_t err, const char *detail)
{
    esp_desktop_buddy_status_reply_t result = {
        .result = esp_desktop_buddy_command_err(err, detail),
    };
    return result;
}

const char *esp_desktop_buddy_command_error_token(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return NULL;
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_SIZE:
        return BUDDY_TOKEN_INVALID_REQUEST;
    case ESP_ERR_NOT_SUPPORTED:
        return BUDDY_TOKEN_UNSUPPORTED;
    case ESP_ERR_NOT_ALLOWED:
        return BUDDY_TOKEN_REJECTED;
    default:
        return BUDDY_TOKEN_INTERNAL;
    }
}

const char *esp_desktop_buddy_status_error_token(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return NULL;
    case ESP_ERR_INVALID_ARG:
        return BUDDY_TOKEN_INVALID_REQUEST;
    case ESP_ERR_INVALID_RESPONSE:
        return BUDDY_TOKEN_INVALID_STATUS_DATA;
    case ESP_ERR_INVALID_SIZE:
        return BUDDY_TOKEN_STATUS_TOO_LARGE;
    case ESP_ERR_NOT_SUPPORTED:
        return BUDDY_TOKEN_UNSUPPORTED;
    case ESP_ERR_NOT_ALLOWED:
        return BUDDY_TOKEN_REJECTED;
    default:
        return BUDDY_TOKEN_INTERNAL;
    }
}

esp_err_t esp_desktop_buddy_new(const esp_desktop_buddy_config_t *config, esp_desktop_buddy_t **out_buddy)
{
    esp_desktop_buddy_t *buddy;
    BaseType_t task_ok;

    if (out_buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    buddy = calloc(1, sizeof(*buddy));
    if (buddy == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (config != NULL) {
        buddy->event_sink = config->event_sink;
        buddy->handlers = config->handlers;
    }

    buddy->inbox_queue = xQueueCreate(BUDDY_INBOX_DEPTH, sizeof(buddy_inbox_item_t));
    buddy->tx_queue = xQueueCreate(BUDDY_TX_QUEUE_DEPTH, sizeof(esp_desktop_buddy_tx_frame_t));
    buddy->state_mutex = xSemaphoreCreateMutex();
    buddy->stop_sem = xSemaphoreCreateBinary();
    if (buddy->inbox_queue == NULL || buddy->tx_queue == NULL ||
        buddy->state_mutex == NULL || buddy->stop_sem == NULL) {
        esp_desktop_buddy_delete(buddy);
        return ESP_ERR_NO_MEM;
    }

    task_ok = xTaskCreatePinnedToCore(buddy_core_task,
                                      "buddy_core",
                                      BUDDY_CORE_TASK_STACK,
                                      buddy,
                                      BUDDY_CORE_TASK_PRIORITY,
                                      &buddy->task,
                                      BUDDY_CORE_TASK_CORE_ID);
    if (task_ok != pdPASS) {
        esp_desktop_buddy_delete(buddy);
        return ESP_ERR_NO_MEM;
    }

    *out_buddy = buddy;
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_transport_port_attach(esp_desktop_buddy_t *buddy)
{
    if (buddy == NULL || buddy->state_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    if (buddy->transport_attached) {
        xSemaphoreGive(buddy->state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    buddy->transport_attached = true;
    xSemaphoreGive(buddy->state_mutex);
    return ESP_OK;
}

void esp_desktop_buddy_transport_port_detach(esp_desktop_buddy_t *buddy)
{
    if (buddy == NULL || buddy->state_mutex == NULL) {
        return;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    buddy->transport_attached = false;
    xSemaphoreGive(buddy->state_mutex);
}

esp_err_t esp_desktop_buddy_delete(esp_desktop_buddy_t *buddy)
{
    buddy_inbox_item_t stop_item = {
        .type = BUDDY_INBOX_ITEM_STOP,
    };
    bool transport_attached = false;

    if (buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (buddy->state_mutex != NULL) {
        xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
        transport_attached = buddy->transport_attached;
        xSemaphoreGive(buddy->state_mutex);
    }
    if (transport_attached) {
        ESP_LOGE(TAG, "refusing to delete core while a transport is still attached");
        return ESP_ERR_INVALID_STATE;
    }

    if (buddy->task != NULL && buddy->inbox_queue != NULL) {
        if (xTaskGetCurrentTaskHandle() == buddy->task) {
            buddy->delete_requested = true;
            return ESP_OK;
        }
        while (xQueueSend(buddy->inbox_queue, &stop_item, pdMS_TO_TICKS(50)) != pdTRUE) {
        }
        if (buddy->stop_sem != NULL) {
            xSemaphoreTake(buddy->stop_sem, portMAX_DELAY);
        }
        buddy->task = NULL;
    }

    if (buddy->inbox_queue != NULL) {
        vQueueDelete(buddy->inbox_queue);
    }
    if (buddy->tx_queue != NULL) {
        vQueueDelete(buddy->tx_queue);
    }
    if (buddy->state_mutex != NULL) {
        vSemaphoreDelete(buddy->state_mutex);
    }
    if (buddy->stop_sem != NULL) {
        vSemaphoreDelete(buddy->stop_sem);
    }
    free(buddy);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_receive_bytes(esp_desktop_buddy_t *buddy, const uint8_t *data, size_t len)
{
    buddy_inbox_item_t item;
    UBaseType_t queue_depth;

    if (buddy == NULL || (data == NULL && len != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (buddy->inbox_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (len > BUDDY_CORE_FEED_RX_CHUNK_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(&item, 0, sizeof(item));
    item.type = BUDDY_INBOX_ITEM_RX;
    item.data.rx.len = len;
    item.data.rx.enqueued_us = esp_timer_get_time();
    memcpy(item.data.rx.bytes, data, len);

    if (xQueueSend(buddy->inbox_queue, &item, 0) != pdTRUE) {
        xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
        buddy->rx_diagnostics.enqueue_failures++;
        xSemaphoreGive(buddy->state_mutex);
        return ESP_ERR_NO_MEM;
    }

    queue_depth = uxQueueMessagesWaiting(buddy->inbox_queue);
    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    buddy->rx_diagnostics.chunks_received++;
    buddy->rx_diagnostics.bytes_received += (uint32_t)len;
    if (len > buddy->rx_diagnostics.max_chunk) {
        buddy->rx_diagnostics.max_chunk = (uint32_t)len;
    }
    if (queue_depth > buddy->rx_diagnostics.queue_high_water) {
        buddy->rx_diagnostics.queue_high_water = queue_depth;
    }
    xSemaphoreGive(buddy->state_mutex);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_get_rx_diagnostics(esp_desktop_buddy_t *buddy,
                                                esp_desktop_buddy_rx_diagnostics_t *out_diagnostics)
{
    if (buddy == NULL || out_diagnostics == NULL || buddy->state_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    *out_diagnostics = buddy->rx_diagnostics;
    xSemaphoreGive(buddy->state_mutex);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_post_permission_reply(esp_desktop_buddy_t *buddy, const esp_desktop_buddy_permission_reply_t *action)
{
    buddy_inbox_item_t item;
    size_t prompt_id_len;

    if (buddy == NULL || action == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (buddy->inbox_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (action->type != ESP_DESKTOP_BUDDY_PERMISSION_REPLY_ONCE &&
        action->type != ESP_DESKTOP_BUDDY_PERMISSION_REPLY_DENY) {
        return ESP_ERR_INVALID_ARG;
    }
    if (action->prompt_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    prompt_id_len = strnlen(action->prompt_id, sizeof(item.data.action.prompt_id));
    if (prompt_id_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (prompt_id_len >= sizeof(item.data.action.prompt_id)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(&item, 0, sizeof(item));
    item.type = BUDDY_INBOX_ITEM_ACTION;
    item.data.action.type = action->type;
    memcpy(item.data.action.prompt_id, action->prompt_id, prompt_id_len + 1);

    if (xQueueSend(buddy->inbox_queue, &item, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t buddy_reply_with_type(esp_desktop_buddy_t *buddy,
                                       const char *prompt_id,
                                       esp_desktop_buddy_permission_reply_type_t type)
{
    esp_desktop_buddy_permission_reply_t action = {
        .type = type,
        .prompt_id = prompt_id,
    };

    return esp_desktop_buddy_post_permission_reply(buddy, &action);
}

esp_err_t esp_desktop_buddy_prompt_approve_once(esp_desktop_buddy_t *buddy, const char *prompt_id)
{
    return buddy_reply_with_type(buddy, prompt_id, ESP_DESKTOP_BUDDY_PERMISSION_REPLY_ONCE);
}

esp_err_t esp_desktop_buddy_prompt_deny(esp_desktop_buddy_t *buddy, const char *prompt_id)
{
    return buddy_reply_with_type(buddy, prompt_id, ESP_DESKTOP_BUDDY_PERMISSION_REPLY_DENY);
}

esp_err_t esp_desktop_buddy_get_snapshot_info(esp_desktop_buddy_t *buddy, esp_desktop_buddy_snapshot_info_t *out_info)
{
    if (buddy == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    out_info->has_state = buddy->state.has_state;
    out_info->live = buddy_state_is_live_at(buddy, buddy_now_ms());
    out_info->prompt_present = buddy->state.prompt.present;
    out_info->prompt_hint_truncated = buddy->state.prompt.hint_truncated;
    out_info->total = buddy->state.total;
    out_info->running = buddy->state.running;
    out_info->waiting = buddy->state.waiting;
    out_info->tokens = buddy->state.tokens;
    out_info->tokens_today = buddy->state.tokens_today;
    out_info->context_value_valid = buddy->state.context_value_valid;
    out_info->context_window_valid = buddy->state.context_window_valid;
    out_info->context_value = buddy->state.context_value;
    out_info->context_window = buddy->state.context_window;
    xSemaphoreGive(buddy->state_mutex);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_get_message(esp_desktop_buddy_t *buddy,
                            char *buf,
                            size_t buf_size,
                            size_t *out_required_size)
{
    esp_err_t err;

    if (buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    if (!buddy->state.has_state) {
        xSemaphoreGive(buddy->state_mutex);
        if (out_required_size != NULL) {
            *out_required_size = 0;
        }
        if (buf != NULL && buf_size != 0) {
            buf[0] = '\0';
        }
        return ESP_ERR_NOT_FOUND;
    }
    err = buddy_copy_string_out(buf, buf_size, buddy->state.msg, out_required_size);
    xSemaphoreGive(buddy->state_mutex);
    return err;
}

esp_err_t esp_desktop_buddy_get_entry_count(esp_desktop_buddy_t *buddy, size_t *out_count)
{
    if (buddy == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    if (!buddy->state.has_state) {
        *out_count = 0;
        xSemaphoreGive(buddy->state_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *out_count = buddy->state.entry_count;
    xSemaphoreGive(buddy->state_mutex);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_get_entry(esp_desktop_buddy_t *buddy,
                          size_t index,
                          char *buf,
                          size_t buf_size,
                          size_t *out_required_size)
{
    esp_err_t err;

    if (buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    if (!buddy->state.has_state || index >= buddy->state.entry_count) {
        xSemaphoreGive(buddy->state_mutex);
        if (out_required_size != NULL) {
            *out_required_size = 0;
        }
        if (buf != NULL && buf_size != 0) {
            buf[0] = '\0';
        }
        return ESP_ERR_NOT_FOUND;
    }
    err = buddy_copy_string_out(buf,
                                buf_size,
                                buddy->state.entries[index],
                                out_required_size);
    xSemaphoreGive(buddy->state_mutex);
    return err;
}

typedef enum {
    BUDDY_PROMPT_FIELD_ID = 0,
    BUDDY_PROMPT_FIELD_TOOL,
    BUDDY_PROMPT_FIELD_HINT,
} buddy_prompt_field_t;

static esp_err_t buddy_get_prompt_field(esp_desktop_buddy_t *buddy,
                                        char *buf,
                                        size_t buf_size,
                                        size_t *out_required_size,
                                        buddy_prompt_field_t field)
{
    esp_err_t err;
    const char *value = NULL;

    if (buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    if (!buddy->state.has_state || !buddy->state.prompt.present) {
        xSemaphoreGive(buddy->state_mutex);
        if (out_required_size != NULL) {
            *out_required_size = 0;
        }
        if (buf != NULL && buf_size != 0) {
            buf[0] = '\0';
        }
        return ESP_ERR_NOT_FOUND;
    }
    switch (field) {
    case BUDDY_PROMPT_FIELD_ID:
        value = buddy->state.prompt.id;
        break;
    case BUDDY_PROMPT_FIELD_TOOL:
        value = buddy->state.prompt.tool;
        break;
    case BUDDY_PROMPT_FIELD_HINT:
        value = buddy->state.prompt.hint;
        break;
    default:
        xSemaphoreGive(buddy->state_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    err = buddy_copy_string_out(buf, buf_size, value, out_required_size);
    xSemaphoreGive(buddy->state_mutex);
    return err;
}

esp_err_t esp_desktop_buddy_get_prompt_id(esp_desktop_buddy_t *buddy,
                              char *buf,
                              size_t buf_size,
                              size_t *out_required_size)
{
    if (buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return buddy_get_prompt_field(buddy,
                                  buf,
                                  buf_size,
                                  out_required_size,
                                  BUDDY_PROMPT_FIELD_ID);
}

esp_err_t esp_desktop_buddy_get_prompt_tool(esp_desktop_buddy_t *buddy,
                                char *buf,
                                size_t buf_size,
                                size_t *out_required_size)
{
    if (buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return buddy_get_prompt_field(buddy,
                                  buf,
                                  buf_size,
                                  out_required_size,
                                  BUDDY_PROMPT_FIELD_TOOL);
}

esp_err_t esp_desktop_buddy_get_prompt_hint(esp_desktop_buddy_t *buddy,
                                char *buf,
                                size_t buf_size,
                                size_t *out_required_size)
{
    if (buddy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return buddy_get_prompt_field(buddy,
                                  buf,
                                  buf_size,
                                  out_required_size,
                                  BUDDY_PROMPT_FIELD_HINT);
}

bool esp_desktop_buddy_is_live(esp_desktop_buddy_t *buddy)
{
    bool live;

    if (buddy == NULL) {
        return false;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    live = buddy_state_is_live_at(buddy, buddy_now_ms());
    xSemaphoreGive(buddy->state_mutex);
    return live;
}
