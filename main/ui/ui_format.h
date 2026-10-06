/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void buddy_app_ui_copy_ellipsized(char *dst,
                                  size_t dst_size,
                                  const char *src,
                                  size_t max_chars);
void buddy_app_ui_format_compact_u64(char *dst, size_t dst_size, uint64_t value);
void buddy_app_ui_copy_or_default(char *dst,
                                  size_t dst_size,
                                  const char *preferred,
                                  const char *fallback);
bool buddy_app_ui_format_clock(bool synced, int32_t offset_seconds, char *time_text,
                               size_t time_text_size,
                               char *date_text,
                               size_t date_text_size,
                               char *second_text,
                               size_t second_text_size);
