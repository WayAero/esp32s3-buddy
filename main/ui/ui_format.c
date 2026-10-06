/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


#include "ui_format.h"
#include "ui_locale.h"

static size_t buddy_app_ui_utf8_prefix_bytes(const char *src,
                                             size_t max_chars,
                                             size_t max_bytes,
                                             bool *truncated)
{
    size_t bytes = 0;
    size_t chars = 0;

    while (src[bytes] != '\0' && chars < max_chars)
    {
        const unsigned char lead = (unsigned char)src[bytes];
        size_t char_bytes = 1;

        if ((lead & 0xE0U) == 0xC0U)
        {
            char_bytes = 2;
        }
        else if ((lead & 0xF0U) == 0xE0U)
        {
            char_bytes = 3;
        }
        else if ((lead & 0xF8U) == 0xF0U)
        {
            char_bytes = 4;
        }
        for (size_t i = 1; i < char_bytes; ++i)
        {
            if (src[bytes + i] == '\0' ||
                ((unsigned char)src[bytes + i] & 0xC0U) != 0x80U)
            {
                char_bytes = 1;
                break;
            }
        }
        if (bytes + char_bytes > max_bytes)
        {
            break;
        }
        bytes += char_bytes;
        chars++;
    }
    if (truncated != NULL)
    {
        *truncated = src[bytes] != '\0';
    }
    return bytes;
}

void buddy_app_ui_copy_ellipsized(char *dst,
                                  size_t dst_size,
                                  const char *src,
                                  size_t max_chars)
{
    bool truncated;
    size_t keep;
    size_t keep_chars;

    if (dst == NULL || dst_size == 0)
    {
        return;
    }
    if (src == NULL || src[0] == '\0')
    {
        dst[0] = '\0';
        return;
    }

    keep = buddy_app_ui_utf8_prefix_bytes(src, max_chars, dst_size - 1, &truncated);
    if (!truncated)
    {
        memcpy(dst, src, keep);
        dst[keep] = '\0';
        return;
    }

    keep_chars = max_chars > 3 ? max_chars - 3 : max_chars;
    keep = buddy_app_ui_utf8_prefix_bytes(src,
                                           keep_chars,
                                           dst_size > 4 ? dst_size - 4 : dst_size - 1,
                                           NULL);
    memcpy(dst, src, keep);
    if (dst_size > 4)
    {
        memcpy(dst + keep, "...", 3);
        dst[keep + 3] = '\0';
    }
    else
    {
        dst[keep] = '\0';
    }
}

void buddy_app_ui_format_compact_u64(char *dst, size_t dst_size, uint64_t value)
{
    static const char suffixes[] = " kMGTPE";
    uint64_t scale = 1;
    size_t suffix_index = 0;

    if (dst == NULL || dst_size == 0)
    {
        return;
    }
    while (suffix_index < 6 && value / scale >= 1000U)
    {
        scale *= 1000U;
        suffix_index++;
    }
    if (suffix_index == 0)
    {
        snprintf(dst, dst_size, "%" PRIu64, value);
        return;
    }
    snprintf(dst,
             dst_size,
             "%" PRIu64 ".%" PRIu64 "%c",
             value / scale,
             (value % scale) / (scale / 10U),
             suffixes[suffix_index]);
}

void buddy_app_ui_copy_or_default(char *dst,
                                  size_t dst_size,
                                  const char *preferred,
                                  const char *fallback)
{
    if (preferred != NULL && preferred[0] != '\0')
    {
        strlcpy(dst, preferred, dst_size);
    }
    else if (fallback != NULL)
    {
        strlcpy(dst, fallback, dst_size);
    }
    else if (dst_size > 0)
    {
        dst[0] = '\0';
    }
}

bool buddy_app_ui_format_clock(bool synced, int32_t offset_seconds, char *time_text,
                               size_t time_text_size,
                               char *date_text,
                               size_t date_text_size,
                               char *second_text,
                               size_t second_text_size)
{
    time_t now = time(NULL);
    struct tm local_tm = {0};

    if (!synced)
    {
        strlcpy(time_text, "--:--", time_text_size);
        strlcpy(date_text, ui_text(UI_TEXT_CLOCK_NOT_SYNCED), date_text_size);
        strlcpy(second_text, "--", second_text_size);
        return false;
    }
    /* 使用快照偏移，避免全局 TZ 修改改变先前事件和当前页面的语义。 */
    now += offset_seconds;
    gmtime_r(&now, &local_tm);
    strftime(time_text, time_text_size, "%H:%M", &local_tm);
    if (ui_locale_get() == UI_LOCALE_ZH_CN)
    {
        static const char *const weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};

        snprintf(date_text,
                 date_text_size,
                 "%d月%d日 周%s",
                 local_tm.tm_mon + 1,
                 local_tm.tm_mday,
                 weekdays[local_tm.tm_wday]);
    }
    else
    {
        strftime(date_text, date_text_size, "%a %b %d", &local_tm);
    }
    strftime(second_text, second_text_size, "%S", &local_tm);
    return true;
}
