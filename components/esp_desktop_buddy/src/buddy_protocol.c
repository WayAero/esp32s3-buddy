/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdlib.h>

#include "buddy_command_extensions_internal.h"
#include "buddy_internal.h"
static const char *TAG = "buddy_protocol";

static esp_err_t buddy_queue_cmd_result_ack(esp_desktop_buddy_t *buddy,
                                            const char *cmd,
                                            esp_desktop_buddy_command_result_t result)
{
    const char *error = esp_desktop_buddy_command_error_token(result.err);

    if (result.err != ESP_OK) {
        ESP_LOGW(TAG,
                 "command handler failed cmd=%s err=%s detail=%s token=%s",
                 cmd,
                 esp_err_to_name(result.err),
                 result.detail != NULL ? result.detail : "<none>",
                 error != NULL ? error : "<none>");
    }

    return buddy_codec_queue_ack(buddy,
                                 cmd,
                                 result.err == ESP_OK,
                                 0,
                                 error);
}

static esp_err_t buddy_queue_command_reply_ack(esp_desktop_buddy_t *buddy,
                                               const char *cmd,
                                               const esp_desktop_buddy_command_ack_t *reply)
{
    return buddy_codec_queue_ack(buddy,
                                 cmd,
                                 reply->ok,
                                 reply->n,
                                 reply->ok ? NULL : reply->error);
}

static esp_err_t buddy_queue_unsupported_command_ack(esp_desktop_buddy_t *buddy, const char *cmd)
{
    return buddy_codec_queue_ack(buddy, cmd, false, 0, BUDDY_TOKEN_UNSUPPORTED);
}

static const esp_desktop_buddy_command_extension_entry_t *buddy_find_command_extension_binding(
    const esp_desktop_buddy_command_extension_set_t *extension,
    const char *cmd)
{
    if (extension == NULL || cmd == NULL || extension->bindings == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < extension->binding_count; ++i) {
        const esp_desktop_buddy_command_extension_entry_t *binding = &extension->bindings[i];

        if (binding->command != NULL && strcmp(binding->command, cmd) == 0) {
            return binding;
        }
    }

    return NULL;
}

static esp_err_t buddy_dispatch_command_extension(esp_desktop_buddy_t *buddy, const char *cmd, cJSON *root)
{
    const esp_desktop_buddy_command_extension_entry_t *binding =
        buddy_find_command_extension_binding(&buddy->handlers.command_extension, cmd);
    esp_desktop_buddy_command_view_t request = {
        .command = cmd,
        .root = root,
    };
    esp_desktop_buddy_command_extension_result_t response;
    const bool is_folder_chunk = strcmp(cmd, "chunk") == 0;
    int64_t handler_started_us = 0;

    if (binding == NULL) {
        return buddy_codec_queue_ack(buddy, cmd, false, 0, BUDDY_TOKEN_UNKNOWN_COMMAND);
    }
    if (binding->handler == NULL) {
        return buddy_queue_unsupported_command_ack(buddy, cmd);
    }

    if (is_folder_chunk) {
        handler_started_us = esp_timer_get_time();
    }
    response = binding->handler(buddy->handlers.command_extension.ctx, buddy, &request);
    if (is_folder_chunk) {
        const uint32_t handler_elapsed_us = (uint32_t)(esp_timer_get_time() - handler_started_us);

        xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
        buddy->rx_diagnostics.folder_chunk_handler_count++;
        buddy->rx_diagnostics.folder_chunk_handler_total_us += handler_elapsed_us;
        if (handler_elapsed_us > buddy->rx_diagnostics.folder_chunk_handler_max_us) {
            buddy->rx_diagnostics.folder_chunk_handler_max_us = handler_elapsed_us;
        }
        xSemaphoreGive(buddy->state_mutex);
    }
    if (response.mode == ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_NO_ACK) {
        return ESP_OK;
    }

    return buddy_queue_command_reply_ack(buddy, cmd, &response.reply);
}

static uint32_t buddy_json_u32(const cJSON *item, bool *ok)
{
    double value;

    if (ok != NULL) {
        *ok = cJSON_IsNumber(item);
    }
    if (!cJSON_IsNumber(item)) {
        return 0;
    }

    value = item->valuedouble;
    if (value <= 0) {
        return 0;
    }
    if (value >= (double)UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)value;
}

static uint64_t buddy_json_u64(const cJSON *item, bool *ok)
{
    double value;
    const double max_value = nextafter((double)UINT64_MAX, 0.0);

    if (ok != NULL) {
        *ok = cJSON_IsNumber(item);
    }
    if (!cJSON_IsNumber(item)) {
        return 0;
    }

    value = item->valuedouble;
    if (value <= 0) {
        return 0;
    }
    if (value >= max_value) {
        return UINT64_MAX;
    }
    return (uint64_t)value;
}

static int64_t buddy_json_i64(const cJSON *item, bool *ok)
{
    double value;
    const double max_value = nextafter((double)INT64_MAX, 0.0);

    if (ok != NULL) {
        *ok = cJSON_IsNumber(item);
    }
    if (!cJSON_IsNumber(item)) {
        return 0;
    }

    value = item->valuedouble;
    if (value <= (double)INT64_MIN) {
        return INT64_MIN;
    }
    if (value >= max_value) {
        return INT64_MAX;
    }
    return (int64_t)value;
}

static int32_t buddy_json_i32(const cJSON *item, bool *ok)
{
    double value;

    if (ok != NULL) {
        *ok = cJSON_IsNumber(item);
    }
    if (!cJSON_IsNumber(item)) {
        return 0;
    }

    value = item->valuedouble;
    if (value <= (double)INT32_MIN) {
        return INT32_MIN;
    }
    if (value >= (double)INT32_MAX) {
        return INT32_MAX;
    }
    return (int32_t)value;
}

static void buddy_apply_entries(cJSON *root, buddy_state_t *state)
{
    cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    cJSON *entry = NULL;

    state->entry_count = 0;
    memset(state->entries, 0, sizeof(state->entries));

    if (!cJSON_IsArray(entries)) {
        return;
    }

    cJSON_ArrayForEach(entry, entries) {
        if (state->entry_count >= BUDDY_STATE_ENTRY_COUNT_MAX) {
            break;
        }
        if (!cJSON_IsString(entry) || entry->valuestring == NULL) {
            continue;
        }
        buddy_copy_utf8_trunc(state->entries[state->entry_count],
                              sizeof(state->entries[0]),
                              entry->valuestring);
        state->entry_count++;
    }
}

static bool buddy_apply_prompt(cJSON *root, buddy_state_t *state)
{
    cJSON *prompt = cJSON_GetObjectItemCaseSensitive(root, "prompt");
    cJSON *id;
    cJSON *tool;
    cJSON *hint;
    size_t prompt_id_len;

    state->prompt = (buddy_prompt_state_t){0};
    if (!cJSON_IsObject(prompt)) {
        return true;
    }

    id = cJSON_GetObjectItemCaseSensitive(prompt, "id");
    if (!cJSON_IsString(id) || id->valuestring == NULL || id->valuestring[0] == '\0') {
        return true;
    }

    // Keep prompt ids exact-fit because they are used for reply matching.
    prompt_id_len = strnlen(id->valuestring, sizeof(state->prompt.id));
    if (prompt_id_len >= sizeof(state->prompt.id)) {
        return false;
    }

    state->prompt.present = true;
    buddy_copy_utf8_trunc(state->prompt.id, sizeof(state->prompt.id), id->valuestring);

    tool = cJSON_GetObjectItemCaseSensitive(prompt, "tool");
    hint = cJSON_GetObjectItemCaseSensitive(prompt, "hint");
    buddy_copy_utf8_trunc(state->prompt.tool,
                          sizeof(state->prompt.tool),
                          cJSON_IsString(tool) ? tool->valuestring : NULL);
    state->prompt.hint_truncated = buddy_copy_utf8_trunc(
        state->prompt.hint, sizeof(state->prompt.hint),
        cJSON_IsString(hint) ? hint->valuestring : NULL);
    return true;
}

static void buddy_handle_state_push(esp_desktop_buddy_t *buddy, cJSON *root)
{
    buddy_state_t next = {0};
    bool ok_total = false;
    bool ok_running = false;
    bool ok_waiting = false;
    bool ok_tokens = false;
    bool ok_tokens_today = false;
    bool protocol_is_v2 = false;
    bool ok_protocol = false;
    bool ok_projected = false;
    bool ok_pressure = false;
    bool ok_context_window = false;
    uint32_t protocol;
    cJSON *msg_item;
    cJSON *context_item;
    bool live_changed = false;
    esp_desktop_buddy_event_t state_event = {
        .type = ESP_DESKTOP_BUDDY_EVENT_SNAPSHOT_UPDATED,
    };
    esp_desktop_buddy_event_t live_event = {
        .type = ESP_DESKTOP_BUDDY_EVENT_LIVENESS_CHANGED,
    };
    int64_t now_ms = buddy_now_ms();

    next.total = buddy_json_u32(cJSON_GetObjectItemCaseSensitive(root, "total"), &ok_total);
    next.running = buddy_json_u32(cJSON_GetObjectItemCaseSensitive(root, "running"), &ok_running);
    next.waiting = buddy_json_u32(cJSON_GetObjectItemCaseSensitive(root, "waiting"), &ok_waiting);
    next.tokens = buddy_json_u64(cJSON_GetObjectItemCaseSensitive(root, "tokens"), &ok_tokens);
    next.tokens_today = buddy_json_u64(cJSON_GetObjectItemCaseSensitive(root, "tokens_today"),
                                       &ok_tokens_today);
    protocol = buddy_json_u32(cJSON_GetObjectItemCaseSensitive(root, "protocol"),
                              &ok_protocol);
    protocol_is_v2 = ok_protocol && protocol == 2U;

    msg_item = cJSON_GetObjectItemCaseSensitive(root, "msg");
    if (!ok_total || !ok_running || !ok_waiting || !ok_tokens ||
        (!protocol_is_v2 && !ok_tokens_today) ||
        !cJSON_IsString(msg_item) || msg_item->valuestring == NULL) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    next.has_state = true;
    buddy_copy_utf8_trunc(next.msg, sizeof(next.msg), msg_item->valuestring);
    if (protocol_is_v2) {
        context_item = cJSON_GetObjectItemCaseSensitive(root, "context");
        if (cJSON_IsObject(context_item)) {
            next.context_value = buddy_json_u64(
                cJSON_GetObjectItemCaseSensitive(context_item, "projected"),
                &ok_projected);
            if (!ok_projected) {
                next.context_value = buddy_json_u64(
                    cJSON_GetObjectItemCaseSensitive(context_item, "pressure"),
                    &ok_pressure);
            }
            next.context_value_valid = ok_projected || ok_pressure;
            next.context_window = buddy_json_u64(
                cJSON_GetObjectItemCaseSensitive(context_item, "window"),
                &ok_context_window);
            next.context_window_valid = ok_context_window && next.context_window != 0;
        }
    }
    buddy_apply_entries(root, &next);
    if (!buddy_apply_prompt(root, &next)) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    xSemaphoreTake(buddy->state_mutex, portMAX_DELAY);
    bool same_prompt = next.prompt.present &&
                       buddy->state.prompt.present &&
                       strcmp(buddy->state.prompt.id, next.prompt.id) == 0;
    if (!next.prompt.present || !same_prompt) {
        buddy->prompt_response_sent = false;
    }
    buddy->state = next;
    buddy->last_state_ms = now_ms;
    live_changed = !buddy->live;
    buddy->live = true;
    live_event.data.live = true;
    xSemaphoreGive(buddy->state_mutex);

    if (!buddy_emit_event(buddy, &state_event)) {
        return;
    }
    if (live_changed) {
        buddy_emit_event(buddy, &live_event);
    }
}

static void buddy_handle_time_sync(esp_desktop_buddy_t *buddy, cJSON *root)
{
    cJSON *time_item = cJSON_GetObjectItemCaseSensitive(root, "time");
    cJSON *epoch;
    cJSON *tz_offset;
    esp_desktop_buddy_event_t event = {
        .type = ESP_DESKTOP_BUDDY_EVENT_TIME_SYNC,
    };
    bool ok_epoch = false;
    bool ok_offset = false;

    if (!cJSON_IsArray(time_item) || cJSON_GetArraySize(time_item) != 2) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    epoch = cJSON_GetArrayItem(time_item, 0);
    tz_offset = cJSON_GetArrayItem(time_item, 1);
    /* 校时单独严格校验；不要复用状态计数的截断/钳位规则。 */
    if (!cJSON_IsNumber(epoch) || !cJSON_IsNumber(tz_offset) ||
        !isfinite(epoch->valuedouble) || !isfinite(tz_offset->valuedouble) ||
        floor(epoch->valuedouble) != epoch->valuedouble ||
        floor(tz_offset->valuedouble) != tz_offset->valuedouble ||
        epoch->valuedouble < ESP_DESKTOP_BUDDY_TIME_EPOCH_MIN || epoch->valuedouble > ESP_DESKTOP_BUDDY_TIME_EPOCH_MAX ||
        tz_offset->valuedouble < ESP_DESKTOP_BUDDY_TIME_OFFSET_MIN || tz_offset->valuedouble > ESP_DESKTOP_BUDDY_TIME_OFFSET_MAX ||
        fmod(tz_offset->valuedouble, ESP_DESKTOP_BUDDY_TIME_OFFSET_STEP) != 0.0) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }
    event.data.time_sync.epoch = buddy_json_i64(epoch, &ok_epoch);
    event.data.time_sync.tz_offset = buddy_json_i32(tz_offset, &ok_offset);
    if (!ok_epoch || !ok_offset) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    buddy_emit_event(buddy, &event);
}

static void buddy_handle_turn_event(esp_desktop_buddy_t *buddy, cJSON *root, size_t line_len)
{
    cJSON *role_item = cJSON_GetObjectItemCaseSensitive(root, "role");
    cJSON *content_item = cJSON_GetObjectItemCaseSensitive(root, "content");
    char *content_json = NULL;
    size_t serialized_len;
    esp_desktop_buddy_event_t event = {
        .type = ESP_DESKTOP_BUDDY_EVENT_TURN,
    };

    if (content_item == NULL) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    content_json = cJSON_PrintUnformatted(content_item);
    if (content_json == NULL) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INTERNAL, 0);
        return;
    }
    serialized_len = strlen(content_json);
    if (serialized_len > BUDDY_TURN_SERIALIZED_LIMIT) {
        ESP_LOGW(TAG,
                 "turn dropped, serialized=%u exceeds %u",
                 (unsigned)serialized_len,
                 (unsigned)BUDDY_TURN_SERIALIZED_LIMIT);
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, (uint32_t)serialized_len);
        cJSON_free(content_json);
        return;
    }

    buddy_copy_utf8_trunc(event.data.turn.role,
                          sizeof(event.data.turn.role),
                          cJSON_IsString(role_item) ? role_item->valuestring : "");
    event.data.turn.content.bytes = (const uint8_t *)content_json;
    event.data.turn.content.len = serialized_len;
    event.data.turn.line_len = (uint32_t)line_len;
    buddy_emit_event(buddy, &event);
    cJSON_free(content_json);
}

static void buddy_handle_event_object(esp_desktop_buddy_t *buddy, cJSON *root, size_t line_len)
{
    cJSON *evt_item = cJSON_GetObjectItemCaseSensitive(root, "evt");
    const char *evt = cJSON_IsString(evt_item) ? evt_item->valuestring : NULL;

    if (evt == NULL) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    if (strcmp(evt, "turn") == 0) {
        buddy_handle_turn_event(buddy, root, line_len);
    }
}

static void buddy_handle_command_object(esp_desktop_buddy_t *buddy, cJSON *root)
{
    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON *name_item;
    const char *cmd = cJSON_IsString(cmd_item) ? cmd_item->valuestring : NULL;
    const char *name = NULL;
    esp_err_t err = ESP_OK;

    if (cmd == NULL) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        if (buddy->handlers.on_status == NULL) {
            err = buddy_codec_queue_ack(buddy,
                                        "status",
                                        false,
                                        0,
                                        BUDDY_TOKEN_UNSUPPORTED);
        } else {
            err = buddy_codec_queue_status_result(
                buddy,
                buddy->handlers.on_status(buddy->handlers.ctx, buddy));
        }
    } else if (strcmp(cmd, "name") == 0) {
        name_item = cJSON_GetObjectItemCaseSensitive(root, "name");
        if (cJSON_IsString(name_item)) {
            name = name_item->valuestring;
        }
        if (name == NULL) {
            err = buddy_codec_queue_ack(buddy, cmd, false, 0, BUDDY_TOKEN_INVALID_REQUEST);
        } else if (buddy->handlers.on_name == NULL) {
            err = buddy_codec_queue_ack(buddy, cmd, false, 0, BUDDY_TOKEN_UNSUPPORTED);
        } else {
            err = buddy_queue_cmd_result_ack(
                buddy,
                cmd,
                buddy->handlers.on_name(buddy->handlers.ctx, buddy, name));
        }
    } else if (strcmp(cmd, "owner") == 0) {
        name_item = cJSON_GetObjectItemCaseSensitive(root, "name");
        if (cJSON_IsString(name_item)) {
            name = name_item->valuestring;
        }
        if (name == NULL) {
            err = buddy_codec_queue_ack(buddy, cmd, false, 0, BUDDY_TOKEN_INVALID_REQUEST);
        } else if (buddy->handlers.on_owner == NULL) {
            err = buddy_codec_queue_ack(buddy, cmd, false, 0, BUDDY_TOKEN_UNSUPPORTED);
        } else {
            err = buddy_queue_cmd_result_ack(
                buddy,
                cmd,
                buddy->handlers.on_owner(buddy->handlers.ctx, buddy, name));
        }
    } else if (strcmp(cmd, "unpair") == 0) {
        if (buddy->handlers.on_unpair == NULL) {
            err = buddy_codec_queue_ack(buddy, cmd, false, 0, BUDDY_TOKEN_UNSUPPORTED);
        } else {
            err = buddy_queue_cmd_result_ack(
                buddy,
                cmd,
                buddy->handlers.on_unpair(buddy->handlers.ctx, buddy));
        }
    } else if (strcmp(cmd, "permission") == 0) {
        return;
    } else {
        err = buddy_dispatch_command_extension(buddy, cmd, root);
    }

    if (err != ESP_OK) {
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_BACKPRESSURE, (uint32_t)err);
    }
}

void buddy_protocol_process_line(esp_desktop_buddy_t *buddy, const char *line, size_t line_len)
{
    cJSON *root;

    buddy_log_protocol_line(TAG, "rx", line, line_len);
    root = cJSON_ParseWithLength(line, line_len);
    if (root == NULL) {
#if CONFIG_ESP_DESKTOP_BUDDY_PROTOCOL_TRACE
        ESP_LOGW(TAG, "rx malformed len=%u", (unsigned)line_len);
#else
        buddy_log_line_preview(TAG, "rx malformed", line, line_len);
#endif
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, 0);
        return;
    }

    if (cJSON_GetObjectItemCaseSensitive(root, "cmd") != NULL) {
        buddy_handle_command_object(buddy, root);
    } else if (cJSON_GetObjectItemCaseSensitive(root, "evt") != NULL) {
        buddy_handle_event_object(buddy, root, line_len);
    } else if (cJSON_GetObjectItemCaseSensitive(root, "time") != NULL) {
        buddy_handle_time_sync(buddy, root);
    } else {
        buddy_handle_state_push(buddy, root);
    }

    cJSON_Delete(root);
}
