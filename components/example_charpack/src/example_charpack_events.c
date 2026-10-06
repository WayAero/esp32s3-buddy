/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "example_charpack_internal.h"

void example_charpack_emit_event(example_charpack_t *charpack,
                                   example_charpack_event_type_t type,
                                   const example_charpack_info_t *info,
                                   const char *path,
                                   uint32_t size,
                                   uint32_t bytes_written,
                                   uint32_t total_bytes)
{
    example_charpack_event_t event = {
        .type = type,
        .path = path,
        .size = size,
        .bytes_written = bytes_written,
        .total_bytes = total_bytes,
    };

    if (charpack == NULL || charpack->on_event == NULL) {
        return;
    }
    if (info != NULL) {
        event.info = *info;
    } else {
        memset(&event.info, 0, sizeof(event.info));
    }

    charpack->on_event(charpack->event_ctx, &event);
}
