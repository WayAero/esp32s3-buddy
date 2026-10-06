/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "cJSON.h"

#include "esp_desktop_buddy/command_extensions.h"

struct esp_desktop_buddy_command_view {
    const char *command;
    const cJSON *root;
};
