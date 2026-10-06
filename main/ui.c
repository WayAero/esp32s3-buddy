/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui/ui_core.h"

esp_err_t buddy_app_ui_init(buddy_app_t *app)
{
    return buddy_app_ui_core_init(app);
}

void buddy_app_ui_start(buddy_app_t *app)
{
    buddy_app_ui_core_start(app);
}
