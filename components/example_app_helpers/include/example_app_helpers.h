/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/transport_ble.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXAMPLE_BUDDY_ENTRY_COUNT_MAX CONFIG_ESP_DESKTOP_BUDDY_ENTRY_COUNT_MAX
#define EXAMPLE_BUDDY_ENTRY_STRING_MAX CONFIG_ESP_DESKTOP_BUDDY_ENTRY_STRING_MAX
#define EXAMPLE_BUDDY_MESSAGE_MAX CONFIG_ESP_DESKTOP_BUDDY_MESSAGE_MAX
#define EXAMPLE_BUDDY_PROMPT_ID_MAX CONFIG_ESP_DESKTOP_BUDDY_PROMPT_ID_MAX
#define EXAMPLE_BUDDY_PROMPT_TOOL_MAX CONFIG_ESP_DESKTOP_BUDDY_PROMPT_TOOL_MAX
#define EXAMPLE_BUDDY_PROMPT_HINT_MAX CONFIG_ESP_DESKTOP_BUDDY_PROMPT_HINT_MAX

typedef struct {
    bool present;
    bool hint_truncated;
    char id[EXAMPLE_BUDDY_PROMPT_ID_MAX + 1];
    char tool[EXAMPLE_BUDDY_PROMPT_TOOL_MAX + 1];
    char hint[EXAMPLE_BUDDY_PROMPT_HINT_MAX + 1];
} example_buddy_prompt_state_t;

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
    char msg[EXAMPLE_BUDDY_MESSAGE_MAX + 1];
    char entries[EXAMPLE_BUDDY_ENTRY_COUNT_MAX][EXAMPLE_BUDDY_ENTRY_STRING_MAX + 1];
    size_t entry_count;
    example_buddy_prompt_state_t prompt;
} example_buddy_state_cache_t;

typedef struct {
    uint16_t velocity_samples[8];
    uint8_t velocity_count;
    uint8_t velocity_next;
    TickType_t prompt_started_tick;
    bool prompt_timing_active;
} example_progress_state_t;

void example_safe_copy(char *dst, size_t dst_size, const char *src);
void example_buddy_state_cache_reset(example_buddy_state_cache_t *state_cache);
esp_err_t example_buddy_state_cache_refresh(esp_desktop_buddy_t *buddy,
                                              example_buddy_state_cache_t *state_cache);
void example_progress_state_reset(example_progress_state_t *progress_state);
void example_progress_clear_prompt_timing(example_progress_state_t *progress_state);
void example_progress_note_snapshot(example_progress_state_t *progress_state,
                                      const example_buddy_state_cache_t *previous_state,
                                      const example_buddy_state_cache_t *current_state);
void example_progress_note_permission_sent(example_progress_state_t *progress_state,
                                             esp_desktop_buddy_permission_decision_t decision);
uint32_t example_progress_velocity(const example_progress_state_t *progress_state);
uint32_t example_progress_level(uint64_t tokens);
esp_err_t example_init_nvs(void);
void example_restore_persisted_string(const char *nvs_ns,
                                        const char *nvs_key,
                                        char *dst,
                                        size_t dst_size,
                                        const char *fallback);

esp_desktop_buddy_command_result_t example_update_string_field(SemaphoreHandle_t mutex,
                                                 char *dst,
                                                 size_t dst_size,
                                                 const char *value);
esp_desktop_buddy_command_result_t example_update_persisted_string(SemaphoreHandle_t mutex,
                                                     char *dst,
                                                     size_t dst_size,
                                                     const char *value,
                                                     const char *nvs_ns,
                                                     const char *nvs_key);
esp_desktop_buddy_command_result_t example_clear_bonds(esp_desktop_buddy_transport_ble_t *transport);
esp_err_t example_reply_current_prompt(SemaphoreHandle_t mutex,
                                         esp_desktop_buddy_t *buddy,
                                         esp_desktop_buddy_transport_ble_t *transport,
                                         const example_buddy_state_cache_t *state_cache,
                                         esp_desktop_buddy_permission_decision_t decision);
void example_note_prompt_response(SemaphoreHandle_t mutex,
                                    uint32_t *approval_count,
                                    uint32_t *denial_count,
                                    esp_desktop_buddy_permission_decision_t decision);
void example_update_transport_state(SemaphoreHandle_t mutex,
                                      esp_desktop_buddy_transport_ble_state_t *transport_state,
                                      char *owner_name,
                                      size_t owner_name_size,
                                      const esp_desktop_buddy_transport_ble_event_t *event);

#ifdef __cplusplus
}
#endif
