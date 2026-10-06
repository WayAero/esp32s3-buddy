/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "buddy_internal.h"

void buddy_linebuf_feed(esp_desktop_buddy_t *buddy, const uint8_t *data, size_t len)
{
    buddy_linebuf_t *linebuf = &buddy->linebuf;

    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];

        if (c == '\r' || c == '\n') {
            if (linebuf->dropping) {
                linebuf->dropping = false;
                linebuf->len = 0;
            } else if (linebuf->len > 0) {
                linebuf->buf[linebuf->len] = '\0';
                buddy_protocol_process_line(buddy, linebuf->buf, linebuf->len);
                linebuf->len = 0;
            }
            continue;
        }

        if (linebuf->dropping) {
            continue;
        }

        if (linebuf->len + 1 >= sizeof(linebuf->buf)) {
            linebuf->len = 0;
            linebuf->dropping = true;
            buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, ESP_DESKTOP_BUDDY_LINE_MAX);
            continue;
        }

        linebuf->buf[linebuf->len++] = c;
    }
}
