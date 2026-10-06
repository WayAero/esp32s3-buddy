/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

typedef enum {
    UI_LOCALE_ZH_CN = 0,
    UI_LOCALE_EN_US,
    UI_LOCALE_COUNT,
} ui_locale_t;

typedef enum {
    UI_TEXT_ACTIVITY,
    UI_TEXT_SETTINGS,
    UI_TEXT_BUDDY,
    UI_TEXT_DIAGNOSTICS,
    UI_TEXT_APP_BUDDY_SUBTITLE,
    UI_TEXT_APP_DIAGNOSTICS_SUBTITLE,
    UI_TEXT_CLOCK_NOT_SYNCED,
    UI_TEXT_NO_RECENT_EVENTS,
    UI_TEXT_SOURCE_PACK,
    UI_TEXT_SOURCE_SYSTEM,
    UI_TEXT_SOURCE_UNKNOWN,
    UI_TEXT_PAIRING_CODE,
    UI_TEXT_APPROVAL_NEEDED,
    UI_TEXT_SECURING,
    UI_TEXT_WORKING,
    UI_TEXT_IDLE,
    UI_TEXT_DISCONNECTED,
    UI_TEXT_TOTAL,
    UI_TEXT_RUNNING,
    UI_TEXT_TOKEN_TOTAL,
    UI_TEXT_CONTEXT,
    UI_TEXT_CURRENT_PACK,
    UI_TEXT_SWITCHING_PACK,
    UI_TEXT_SWITCH_FAILED,
    UI_TEXT_WAITING_COUNT_FMT,
    UI_TEXT_REPLY_FAILED,
    UI_TEXT_PROMPT_INCOMPLETE,
    UI_TEXT_NO_SESSION,
    UI_TEXT_READY,
    UI_TEXT_PASSKEY,
    UI_TEXT_ENTER_ON_COMPUTER,
    UI_TEXT_REQUEST_FMT,
    UI_TEXT_APPROVAL,
    UI_TEXT_APPROVE_REQUEST,
    UI_TEXT_WAITING,
    UI_TEXT_NO_PENDING_REQUEST,
    UI_TEXT_PAIR_IN_CLAUDE,
    UI_TEXT_ALLOW_ONCE,
    UI_TEXT_DENY,
    UI_TEXT_NO_PACK,
    UI_TEXT_GIF_MISSING,
    UI_TEXT_GIF_FAILED,
    UI_TEXT_GIF_OFF,
    UI_TEXT_HOME_ACTIVITY_FMT,
    UI_TEXT_DIAG_SERVICES_OK,
    UI_TEXT_DIAG_SERVICES_ERROR,
    UI_TEXT_EVENT_SENSITIVE_OMITTED,
    UI_TEXT_EVENT_PERMISSION_RECEIVED,
    UI_TEXT_EVENT_PERMISSION_CLOSED,
    UI_TEXT_EVENT_BUDDY_DISCONNECTED,
    UI_TEXT_EVENT_PERMISSION_APPROVED,
    UI_TEXT_EVENT_PERMISSION_DENIED,
    UI_TEXT_EVENT_PROMPT_BACKEND_FAILED,
    UI_TEXT_EVENT_BUDDY_PROTOCOL_ERROR,
    UI_TEXT_EVENT_BLE_CONNECTED,
    UI_TEXT_EVENT_BLE_DISCONNECTED,
    UI_TEXT_EVENT_BLE_ENCRYPTED,
    UI_TEXT_EVENT_PACK_INSTALLED,
    UI_TEXT_EVENT_PACK_INSTALL_FAILED,
    UI_TEXT_EVENT_PACK_TRANSFER_STARTED,
    UI_TEXT_EVENT_PACK_CHANGED,
    UI_TEXT_EVENT_PACK_CLEARED,
    UI_TEXT_EVENT_SETTINGS_RESET,
    UI_TEXT_EVENT_PROMPT_REPLY_IGNORED,
    UI_TEXT_EVENT_PROMPT_QUEUE_FULL,
    UI_TEXT_EVENT_PROMPT_EXPIRED,
    UI_TEXT_EVENT_PROMPT_FAILED,
    UI_TEXT_EVENT_PROMPT_APPROVAL_QUEUED,
    UI_TEXT_EVENT_PROMPT_DENIAL_QUEUED,
    UI_TEXT_BLE_DEVICE_NAME,
    UI_TEXT_COUNT,
} ui_text_id_t;

void ui_locale_init(void);
ui_locale_t ui_locale_get(void);
esp_err_t ui_locale_apply(ui_locale_t locale);
esp_err_t ui_locale_save(ui_locale_t locale);
esp_err_t ui_locale_set(ui_locale_t locale);
void ui_locale_reset(void);
esp_err_t ui_locale_clear_saved(void);
const char *ui_locale_code(ui_locale_t locale);
const char *ui_locale_weather_language(void);
const char *ui_text(ui_text_id_t id);
const char *ui_text_for(ui_locale_t locale, ui_text_id_t id);

void ui_locale_font_init(void);
const lv_font_t *ui_locale_font(const lv_font_t *latin_font);
const lv_font_t *ui_locale_static_font(const lv_font_t *latin_font);
bool ui_locale_dynamic_font_available(void);

/* 外部正文使用统一 14px 中英文字体，由 UI 任务调用。 */
const lv_font_t *ui_locale_content_font(void);
