/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/transport_ble.h"
#include "esp_err.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "example_app_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*example_console_state_fn_t)(void *ctx, FILE *out);
typedef int (*example_console_cmd_fn_t)(void *ctx, int argc, char **argv);

typedef struct {
    const char *command;
    const char *help;
    const char *hint;
    example_console_cmd_fn_t handler;
} example_console_command_t;

typedef struct {
    SemaphoreHandle_t mutex;
    esp_desktop_buddy_t *buddy;
    esp_desktop_buddy_transport_ble_t *transport;
    const example_buddy_state_cache_t *state_cache;
    example_console_state_fn_t print_state;
    void *state_ctx;
} example_console_common_cmds_t;

typedef struct {
    const char *prompt;
    const char *banner;
    const example_console_command_t *commands;
    size_t command_count;
    const example_console_common_cmds_t *common_cmds;
    void *ctx;
    uint32_t task_stack_size;
    uint32_t task_priority;
    size_t max_cmdline_length;
} example_console_config_t;

void example_console_init(void);
esp_err_t example_console_start(example_console_config_t *config);

#ifdef __cplusplus
}
#endif
