/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "example_app_helpers.h"

#include "nvs_flash.h"
#include "example_store_nvs.h"

static uint32_t example_progress_seconds_since(TickType_t started_tick)
{
    TickType_t elapsed_ticks = xTaskGetTickCount() - started_tick;

    return (uint32_t)(pdTICKS_TO_MS(elapsed_ticks) / 1000U);
}

static void example_progress_store_velocity(example_progress_state_t *progress_state, uint32_t seconds)
{
    if (progress_state == NULL) {
        return;
    }

    progress_state->velocity_samples[progress_state->velocity_next] =
        (uint16_t)(seconds > UINT16_MAX ? UINT16_MAX : seconds);
    progress_state->velocity_next = (progress_state->velocity_next + 1U) % 8U;
    if (progress_state->velocity_count < 8U) {
        progress_state->velocity_count++;
    }
}

static esp_desktop_buddy_command_result_t example_missing_name_result(void)
{
    return esp_desktop_buddy_command_err(ESP_ERR_INVALID_ARG, "missing_name");
}

static esp_desktop_buddy_command_result_t example_name_too_long_result(void)
{
    return esp_desktop_buddy_command_err(ESP_ERR_INVALID_ARG, "name_too_long");
}

static bool example_string_fits(size_t dst_size, const char *value)
{
    return value != NULL && strnlen(value, dst_size) < dst_size;
}

static void example_store_string(SemaphoreHandle_t mutex,
                                   char *dst,
                                   size_t dst_size,
                                   const char *value)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    example_safe_copy(dst, dst_size, value);
    xSemaphoreGive(mutex);
}

void example_safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strlcpy(dst, src, dst_size);
}

void example_buddy_state_cache_reset(example_buddy_state_cache_t *state_cache)
{
    if (state_cache == NULL) {
        return;
    }

    memset(state_cache, 0, sizeof(*state_cache));
}

esp_err_t example_buddy_state_cache_refresh(esp_desktop_buddy_t *buddy,
                                              example_buddy_state_cache_t *state_cache)
{
    esp_desktop_buddy_snapshot_info_t info = {0};
    size_t entry_count = 0;
    esp_err_t err;

    if (buddy == NULL || state_cache == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    example_buddy_state_cache_reset(state_cache);

    err = esp_desktop_buddy_get_snapshot_info(buddy, &info);
    if (err != ESP_OK) {
        return err;
    }

    state_cache->has_state = info.has_state;
    state_cache->total = info.total;
    state_cache->running = info.running;
    state_cache->waiting = info.waiting;
    state_cache->tokens = info.tokens;
    state_cache->tokens_today = info.tokens_today;
    state_cache->context_value_valid = info.context_value_valid;
    state_cache->context_window_valid = info.context_window_valid;
    state_cache->context_value = info.context_value;
    state_cache->context_window = info.context_window;
    state_cache->prompt.present = info.prompt_present;
    state_cache->prompt.hint_truncated = info.prompt_hint_truncated;

    if (!info.has_state) {
        return ESP_OK;
    }

    err = esp_desktop_buddy_get_message(buddy, state_cache->msg, sizeof(state_cache->msg), NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_desktop_buddy_get_entry_count(buddy, &entry_count);
    if (err != ESP_OK) {
        return err;
    }
    if (entry_count > EXAMPLE_BUDDY_ENTRY_COUNT_MAX) {
        entry_count = EXAMPLE_BUDDY_ENTRY_COUNT_MAX;
    }
    state_cache->entry_count = entry_count;
    for (size_t i = 0; i < entry_count; ++i) {
        err = esp_desktop_buddy_get_entry(buddy,
                              i,
                              state_cache->entries[i],
                              sizeof(state_cache->entries[i]),
                              NULL);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (!info.prompt_present) {
        return ESP_OK;
    }

    err = esp_desktop_buddy_get_prompt_id(buddy,
                              state_cache->prompt.id,
                              sizeof(state_cache->prompt.id),
                              NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_desktop_buddy_get_prompt_tool(buddy,
                                state_cache->prompt.tool,
                                sizeof(state_cache->prompt.tool),
                                NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_desktop_buddy_get_prompt_hint(buddy,
                                state_cache->prompt.hint,
                                sizeof(state_cache->prompt.hint),
                                NULL);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

void example_progress_state_reset(example_progress_state_t *progress_state)
{
    if (progress_state == NULL) {
        return;
    }

    memset(progress_state, 0, sizeof(*progress_state));
}

void example_progress_clear_prompt_timing(example_progress_state_t *progress_state)
{
    if (progress_state == NULL) {
        return;
    }

    progress_state->prompt_timing_active = false;
}

void example_progress_note_snapshot(example_progress_state_t *progress_state,
                                      const example_buddy_state_cache_t *previous_state,
                                      const example_buddy_state_cache_t *current_state)
{
    bool prompt_changed;

    if (progress_state == NULL || current_state == NULL) {
        return;
    }

    prompt_changed = previous_state == NULL ||
                     previous_state->prompt.present != current_state->prompt.present ||
                     strcmp(previous_state->prompt.id, current_state->prompt.id) != 0;

    if (current_state->prompt.present) {
        if (prompt_changed) {
            progress_state->prompt_started_tick = xTaskGetTickCount();
            progress_state->prompt_timing_active = true;
        }
        return;
    }

    progress_state->prompt_timing_active = false;
}

void example_progress_note_permission_sent(example_progress_state_t *progress_state,
                                             esp_desktop_buddy_permission_decision_t decision)
{
    if (progress_state == NULL) {
        return;
    }

    if (decision == ESP_DESKTOP_BUDDY_PERMISSION_DECISION_ONCE &&
        progress_state->prompt_timing_active) {
        example_progress_store_velocity(progress_state,
                                          example_progress_seconds_since(progress_state->prompt_started_tick));
    }

    progress_state->prompt_timing_active = false;
}

uint32_t example_progress_velocity(const example_progress_state_t *progress_state)
{
    uint16_t tmp[8];
    uint8_t count;

    if (progress_state == NULL || progress_state->velocity_count == 0) {
        return 0;
    }

    count = progress_state->velocity_count;
    memcpy(tmp, progress_state->velocity_samples, count * sizeof(tmp[0]));
    for (uint8_t i = 1; i < count; ++i) {
        uint16_t key = tmp[i];
        int j = (int)i - 1;

        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            --j;
        }
        tmp[j + 1] = key;
    }

    return tmp[count / 2U];
}

uint32_t example_progress_level(uint64_t tokens)
{
    return (uint32_t)(tokens / 50000ULL);
}


void example_restore_persisted_string(const char *nvs_ns,
                                        const char *nvs_key,
                                        char *dst,
                                        size_t dst_size,
                                        const char *fallback)
{
    if (example_store_nvs_read_string(nvs_ns, nvs_key, dst, dst_size) != ESP_OK) {
        example_safe_copy(dst, dst_size, fallback);
    }
}

esp_desktop_buddy_command_result_t example_update_string_field(SemaphoreHandle_t mutex,
                                                 char *dst,
                                                 size_t dst_size,
                                                 const char *value)
{
    if (mutex == NULL || dst == NULL || dst_size == 0) {
        return esp_desktop_buddy_command_err(ESP_FAIL, "invalid_destination");
    }
    if (value == NULL || value[0] == '\0') {
        return example_missing_name_result();
    }
    if (!example_string_fits(dst_size, value)) {
        return example_name_too_long_result();
    }

    example_store_string(mutex, dst, dst_size, value);
    return esp_desktop_buddy_command_ok();
}

esp_desktop_buddy_command_result_t example_update_persisted_string(SemaphoreHandle_t mutex,
                                                     char *dst,
                                                     size_t dst_size,
                                                     const char *value,
                                                     const char *nvs_ns,
                                                     const char *nvs_key)
{
    esp_err_t err;
    char previous[dst_size];

    if (mutex == NULL || dst == NULL || dst_size == 0 || nvs_ns == NULL || nvs_key == NULL) {
        return esp_desktop_buddy_command_err(ESP_FAIL, "invalid_persist_target");
    }
    if (value == NULL || value[0] == '\0') {
        return example_missing_name_result();
    }
    if (!example_string_fits(dst_size, value)) {
        return example_name_too_long_result();
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    example_safe_copy(previous, sizeof(previous), dst);
    example_safe_copy(dst, dst_size, value);
    err = example_store_nvs_write_string(nvs_ns, nvs_key, dst);
    if (err != ESP_OK) {
        example_safe_copy(dst, dst_size, previous);
    }
    xSemaphoreGive(mutex);
    return err == ESP_OK ? esp_desktop_buddy_command_ok() : esp_desktop_buddy_command_err(ESP_FAIL, "persist_failed");
}

esp_desktop_buddy_command_result_t example_clear_bonds(esp_desktop_buddy_transport_ble_t *transport)
{
    if (transport == NULL) {
        return esp_desktop_buddy_command_err(ESP_FAIL, "missing_transport");
    }

    return esp_desktop_buddy_transport_ble_clear_bonds(transport) == ESP_OK
               ? esp_desktop_buddy_command_ok()
               : esp_desktop_buddy_command_err(ESP_FAIL, "clear_failed");
}

esp_err_t example_reply_current_prompt(SemaphoreHandle_t mutex,
                                         esp_desktop_buddy_t *buddy,
                                         esp_desktop_buddy_transport_ble_t *transport,
                                         const example_buddy_state_cache_t *state_cache,
                                         esp_desktop_buddy_permission_decision_t decision)
{
    esp_err_t err = ESP_ERR_INVALID_STATE;
    esp_desktop_buddy_transport_ble_state_t transport_state = {0};

    if (mutex == NULL || buddy == NULL || transport == NULL || state_cache == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (state_cache->prompt.present &&
        esp_desktop_buddy_is_live(buddy) &&
        esp_desktop_buddy_transport_ble_get_state(transport, &transport_state) == ESP_OK &&
        transport_state.tx_ready) {
        err = decision == ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY
                  ? esp_desktop_buddy_prompt_deny(buddy, state_cache->prompt.id)
                  : esp_desktop_buddy_prompt_approve_once(buddy, state_cache->prompt.id);
    }
    xSemaphoreGive(mutex);

    return err;
}

void example_note_prompt_response(SemaphoreHandle_t mutex,
                                    uint32_t *approval_count,
                                    uint32_t *denial_count,
                                    esp_desktop_buddy_permission_decision_t decision)
{
    if (mutex == NULL || approval_count == NULL || denial_count == NULL) {
        return;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (decision == ESP_DESKTOP_BUDDY_PERMISSION_DECISION_DENY) {
        (*denial_count)++;
    } else {
        (*approval_count)++;
    }
    xSemaphoreGive(mutex);
}

void example_update_transport_state(SemaphoreHandle_t mutex,
                                      esp_desktop_buddy_transport_ble_state_t *transport_state,
                                      char *owner_name,
                                      size_t owner_name_size,
                                      const esp_desktop_buddy_transport_ble_event_t *event)
{
    bool disconnected;

    if (mutex == NULL || transport_state == NULL || event == NULL) {
        return;
    }
    disconnected = (event->changed_fields & ESP_DESKTOP_BUDDY_TRANSPORT_BLE_FIELD_CONNECTED) != 0 &&
                   !event->state.connected;

    xSemaphoreTake(mutex, portMAX_DELAY);
    *transport_state = event->state;
    if (disconnected && owner_name != NULL && owner_name_size > 0) {
        example_safe_copy(owner_name, owner_name_size, NULL);
    }
    xSemaphoreGive(mutex);
}

esp_err_t example_init_nvs(void)
{
    /* 初始化失败交给调用者诊断，不能自动清空配对、校准和设置。 */
    return nvs_flash_init();
}
