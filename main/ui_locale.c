/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_locale.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "src/font/binfont_loader/lv_binfont_loader.h"

#define UI_LOCALE_NVS_NAMESPACE "buddy_cfg"
#define UI_LOCALE_NVS_KEY "locale"
#define UI_LOCALE_DYNAMIC_FONT_PATH "S:fonts/zh_cn_14.bin"
#define UI_LOCALE_DYNAMIC_FONT_VERSION 4
#define UI_LOCALE_DYNAMIC_FONT_LINE_HEIGHT 17
#define UI_LOCALE_DYNAMIC_FONT_V3_SENTINEL 0x839E /* 莞 */
#define UI_LOCALE_FONT_WRAPPER_COUNT 8

LV_FONT_DECLARE(ui_font_zh_12);
LV_FONT_DECLARE(ui_font_zh_14);
LV_FONT_DECLARE(ui_font_zh_18);

static const char *TAG = "ui_locale";
static portMUX_TYPE s_locale_lock = portMUX_INITIALIZER_UNLOCKED;
static ui_locale_t s_locale = UI_LOCALE_ZH_CN;
static lv_font_t *s_dynamic_font;
/* 不经过动态回退链，避免正文主字体与固定字体形成循环。 */
static lv_font_t s_content_fallback;
static lv_font_t s_fixed_font_12;
static lv_font_t s_fixed_font_14;
static lv_font_t s_fixed_font_18;
static bool s_fixed_fonts_initialized;

typedef struct {
    const lv_font_t *source;
    lv_font_t font;
} ui_locale_font_wrapper_t;

static ui_locale_font_wrapper_t s_font_wrappers[UI_LOCALE_FONT_WRAPPER_COUNT];

extern const uint8_t _binary_ui_zh_cn_14_bin_start[];
extern const uint8_t _binary_ui_zh_cn_14_bin_end[];

static bool ui_locale_dynamic_font_valid(const lv_font_t *font)
{
    static const uint32_t required[] = {
        0x8BBE, /* 设 */
        0x7F6E, /* 置 */
        0x8BED, /* 语 */
        0x8A00, /* 言 */
        0x7F51, /* 网 */
        0x7EDC, /* 络 */
        0x4E1C, /* 东 */
        0x6674, /* 晴 */
        UI_LOCALE_DYNAMIC_FONT_V3_SENTINEL,
        0x3001, /* 、 */
        0xFF08, /* （ */
        0xFF09, /* ） */
        0x201C, /* “ */
        0x201D, /* ” */
    };
    lv_font_glyph_dsc_t glyph;

    if (font == NULL || font->line_height != UI_LOCALE_DYNAMIC_FONT_LINE_HEIGHT) {
        return false;
    }
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (!lv_font_get_glyph_dsc(font, &glyph, required[i], 0) ||
            glyph.is_placeholder || glyph.resolved_font != font) {
            return false;
        }
    }
    return true;
}

static void ui_locale_prepare_fixed_fonts(void)
{
    if (!s_fixed_fonts_initialized) {
        s_fixed_font_12 = ui_font_zh_12;
        s_fixed_font_14 = ui_font_zh_14;
        s_fixed_font_18 = ui_font_zh_18;
        s_fixed_fonts_initialized = true;
    }
    s_fixed_font_12.fallback = s_dynamic_font != NULL ? s_dynamic_font : ui_font_zh_12.fallback;
    s_fixed_font_14.fallback = s_dynamic_font != NULL ? s_dynamic_font : ui_font_zh_14.fallback;
    s_fixed_font_18.fallback = s_dynamic_font != NULL ? s_dynamic_font : ui_font_zh_18.fallback;
}

static lv_font_t *ui_locale_fixed_font_for(const lv_font_t *latin_font)
{
    uint16_t line_height = latin_font != NULL ? latin_font->line_height : 14;

    ui_locale_prepare_fixed_fonts();
    if (line_height <= 13) {
        return &s_fixed_font_12;
    }
    if (line_height <= 16) {
        return &s_fixed_font_14;
    }
    return &s_fixed_font_18;
}

static const lv_font_t *ui_locale_layered_font(const lv_font_t *latin_font)
{
    if (latin_font == NULL) {
        return ui_locale_fixed_font_for(NULL);
    }
    for (size_t i = 0; i < UI_LOCALE_FONT_WRAPPER_COUNT; ++i) {
        if (s_font_wrappers[i].source == latin_font) {
            return &s_font_wrappers[i].font;
        }
        if (s_font_wrappers[i].source == NULL) {
            s_font_wrappers[i].source = latin_font;
            s_font_wrappers[i].font = *latin_font;
            s_font_wrappers[i].font.fallback = ui_locale_fixed_font_for(latin_font);
            return &s_font_wrappers[i].font;
        }
    }
    ESP_LOGW(TAG, "locale font wrapper capacity exhausted");
    return ui_locale_fixed_font_for(latin_font);
}

static const char *const s_text_en[UI_TEXT_COUNT] = {
    [UI_TEXT_ACTIVITY] = "Activity",
    [UI_TEXT_SETTINGS] = "Settings",
    [UI_TEXT_BUDDY] = "Buddy",
    [UI_TEXT_DIAGNOSTICS] = "Diagnostics",
    [UI_TEXT_APP_BUDDY_SUBTITLE] = "Permissions and session status",
    [UI_TEXT_APP_DIAGNOSTICS_SUBTITLE] = "System health and services",
    [UI_TEXT_CLOCK_NOT_SYNCED] = "Clock not synced",
    [UI_TEXT_NO_RECENT_EVENTS] = "No recent events",
    [UI_TEXT_SOURCE_PACK] = "Pack",
    [UI_TEXT_SOURCE_SYSTEM] = "System",
    [UI_TEXT_SOURCE_UNKNOWN] = "Unknown",
    [UI_TEXT_PAIRING_CODE] = "Pairing code",
    [UI_TEXT_APPROVAL_NEEDED] = "Approval needed",
    [UI_TEXT_SECURING] = "Securing",
    [UI_TEXT_WORKING] = "Working",
    [UI_TEXT_IDLE] = "Idle",
    [UI_TEXT_DISCONNECTED] = "Disconnected",
    [UI_TEXT_TOTAL] = "Total",
    [UI_TEXT_RUNNING] = "Running",
    [UI_TEXT_TOKEN_TOTAL] = "Token",
    [UI_TEXT_CONTEXT] = "Context",
    [UI_TEXT_CURRENT_PACK] = "Current pack",
    [UI_TEXT_SWITCHING_PACK] = "Switching pack...",
    [UI_TEXT_SWITCH_FAILED] = "Switch failed",
    [UI_TEXT_WAITING_COUNT_FMT] = "%lu waiting",
    [UI_TEXT_REPLY_FAILED] = "Reply failed, retry",
    [UI_TEXT_PROMPT_INCOMPLETE] = "Description incomplete",
    [UI_TEXT_NO_SESSION] = "No session",
    [UI_TEXT_READY] = "Ready",
    [UI_TEXT_PASSKEY] = "Passkey",
    [UI_TEXT_ENTER_ON_COMPUTER] = "Enter in the computer pairing dialog",
    [UI_TEXT_REQUEST_FMT] = "%s request",
    [UI_TEXT_APPROVAL] = "Approval",
    [UI_TEXT_APPROVE_REQUEST] = "Approve this request?",
    [UI_TEXT_WAITING] = "Waiting",
    [UI_TEXT_NO_PENDING_REQUEST] = "No pending request",
    [UI_TEXT_PAIR_IN_CLAUDE] = "Pair in Claude",
    [UI_TEXT_ALLOW_ONCE] = "ALLOW ONCE",
    [UI_TEXT_DENY] = "DENY",
    [UI_TEXT_NO_PACK] = "No pack",
    [UI_TEXT_GIF_MISSING] = "GIF missing",
    [UI_TEXT_GIF_FAILED] = "GIF failed",
    [UI_TEXT_GIF_OFF] = "GIF off",
    [UI_TEXT_HOME_ACTIVITY_FMT] = "Activity\n%s",
    [UI_TEXT_DIAG_SERVICES_OK] = "Services healthy",
    [UI_TEXT_DIAG_SERVICES_ERROR] = "Service unavailable",
    [UI_TEXT_EVENT_SENSITIVE_OMITTED] = "Sensitive event omitted",
    [UI_TEXT_EVENT_PERMISSION_RECEIVED] = "Permission request received",
    [UI_TEXT_EVENT_PERMISSION_CLOSED] = "Permission request closed",
    [UI_TEXT_EVENT_BUDDY_DISCONNECTED] = "Buddy disconnected",
    [UI_TEXT_EVENT_PERMISSION_APPROVED] = "Permission approved",
    [UI_TEXT_EVENT_PERMISSION_DENIED] = "Permission denied",
    [UI_TEXT_EVENT_PROMPT_BACKEND_FAILED] = "Prompt backend failed",
    [UI_TEXT_EVENT_BUDDY_PROTOCOL_ERROR] = "Buddy protocol error",
    [UI_TEXT_EVENT_BLE_CONNECTED] = "BLE connected",
    [UI_TEXT_EVENT_BLE_DISCONNECTED] = "BLE disconnected",
    [UI_TEXT_EVENT_BLE_ENCRYPTED] = "BLE encrypted",
    [UI_TEXT_EVENT_PACK_INSTALLED] = "Character pack installed",
    [UI_TEXT_EVENT_PACK_INSTALL_FAILED] = "Pack install failed",
    [UI_TEXT_EVENT_PACK_TRANSFER_STARTED] = "Pack transfer started",
    [UI_TEXT_EVENT_PACK_CHANGED] = "Character pack changed",
    [UI_TEXT_EVENT_PACK_CLEARED] = "Character pack cleared",
    [UI_TEXT_EVENT_SETTINGS_RESET] = "Settings reset",
    [UI_TEXT_EVENT_PROMPT_REPLY_IGNORED] = "Prompt reply ignored",
    [UI_TEXT_EVENT_PROMPT_QUEUE_FULL] = "Prompt reply queue full",
    [UI_TEXT_EVENT_PROMPT_EXPIRED] = "Prompt response expired",
    [UI_TEXT_EVENT_PROMPT_FAILED] = "Prompt response failed",
    [UI_TEXT_EVENT_PROMPT_APPROVAL_QUEUED] = "Prompt approval queued",
    [UI_TEXT_EVENT_PROMPT_DENIAL_QUEUED] = "Prompt denial queued",
    [UI_TEXT_BLE_DEVICE_NAME] = "Bluetooth name",
};

static const char *const s_text_zh[UI_TEXT_COUNT] = {
    [UI_TEXT_ACTIVITY] = "活动",
    [UI_TEXT_SETTINGS] = "设置",
    [UI_TEXT_BUDDY] = "伙伴",
    [UI_TEXT_DIAGNOSTICS] = "诊断",
    [UI_TEXT_APP_BUDDY_SUBTITLE] = "权限审批与会话状态",
    [UI_TEXT_APP_DIAGNOSTICS_SUBTITLE] = "系统状态与服务",
    [UI_TEXT_CLOCK_NOT_SYNCED] = "时间未同步",
    [UI_TEXT_NO_RECENT_EVENTS] = "暂无事件",
    [UI_TEXT_SOURCE_PACK] = "角色包",
    [UI_TEXT_SOURCE_SYSTEM] = "系统",
    [UI_TEXT_SOURCE_UNKNOWN] = "未知",
    [UI_TEXT_PAIRING_CODE] = "配对码",
    [UI_TEXT_APPROVAL_NEEDED] = "等待审批",
    [UI_TEXT_SECURING] = "正在建立安全连接",
    [UI_TEXT_WORKING] = "工作中",
    [UI_TEXT_IDLE] = "空闲",
    [UI_TEXT_DISCONNECTED] = "未连接",
    [UI_TEXT_TOTAL] = "会话",
    [UI_TEXT_RUNNING] = "运行",
    [UI_TEXT_TOKEN_TOTAL] = "Token",
    [UI_TEXT_CONTEXT] = "上下文",
    [UI_TEXT_CURRENT_PACK] = "当前角色包",
    [UI_TEXT_SWITCHING_PACK] = "正在切换角色包...",
    [UI_TEXT_SWITCH_FAILED] = "切换失败",
    [UI_TEXT_WAITING_COUNT_FMT] = "%lu 项待审批",
    [UI_TEXT_REPLY_FAILED] = "回复失败，请重试",
    [UI_TEXT_PROMPT_INCOMPLETE] = "说明未完整显示",
    [UI_TEXT_NO_SESSION] = "暂无会话",
    [UI_TEXT_READY] = "就绪",
    [UI_TEXT_PASSKEY] = "配对码",
    [UI_TEXT_ENTER_ON_COMPUTER] = "在电脑蓝牙配对窗口输入",
    [UI_TEXT_REQUEST_FMT] = "%s 请求",
    [UI_TEXT_APPROVAL] = "审批",
    [UI_TEXT_APPROVE_REQUEST] = "允许此请求？",
    [UI_TEXT_WAITING] = "等待中",
    [UI_TEXT_NO_PENDING_REQUEST] = "暂无请求",
    [UI_TEXT_PAIR_IN_CLAUDE] = "请在 Claude 配对",
    [UI_TEXT_ALLOW_ONCE] = "仅允许一次",
    [UI_TEXT_DENY] = "拒绝",
    [UI_TEXT_NO_PACK] = "暂无角色包",
    [UI_TEXT_GIF_MISSING] = "动画文件缺失",
    [UI_TEXT_GIF_FAILED] = "动画加载失败",
    [UI_TEXT_GIF_OFF] = "动画已关闭",
    [UI_TEXT_HOME_ACTIVITY_FMT] = "最近活动\n%s",
    [UI_TEXT_DIAG_SERVICES_OK] = "服务正常",
    [UI_TEXT_DIAG_SERVICES_ERROR] = "服务不可用",
    [UI_TEXT_EVENT_SENSITIVE_OMITTED] = "敏感事件已隐藏",
    [UI_TEXT_EVENT_PERMISSION_RECEIVED] = "收到权限请求",
    [UI_TEXT_EVENT_PERMISSION_CLOSED] = "权限请求已关闭",
    [UI_TEXT_EVENT_BUDDY_DISCONNECTED] = "伙伴连接已断开",
    [UI_TEXT_EVENT_PERMISSION_APPROVED] = "已允许权限请求",
    [UI_TEXT_EVENT_PERMISSION_DENIED] = "已拒绝权限请求",
    [UI_TEXT_EVENT_PROMPT_BACKEND_FAILED] = "审批后端失败",
    [UI_TEXT_EVENT_BUDDY_PROTOCOL_ERROR] = "伙伴协议错误",
    [UI_TEXT_EVENT_BLE_CONNECTED] = "蓝牙已连接",
    [UI_TEXT_EVENT_BLE_DISCONNECTED] = "蓝牙已断开",
    [UI_TEXT_EVENT_BLE_ENCRYPTED] = "蓝牙已加密",
    [UI_TEXT_EVENT_PACK_INSTALLED] = "角色包已安装",
    [UI_TEXT_EVENT_PACK_INSTALL_FAILED] = "角色包安装失败",
    [UI_TEXT_EVENT_PACK_TRANSFER_STARTED] = "开始接收角色包",
    [UI_TEXT_EVENT_PACK_CHANGED] = "角色包已切换",
    [UI_TEXT_EVENT_PACK_CLEARED] = "角色包已清除",
    [UI_TEXT_EVENT_SETTINGS_RESET] = "设置已重置",
    [UI_TEXT_EVENT_PROMPT_REPLY_IGNORED] = "审批回复已忽略",
    [UI_TEXT_EVENT_PROMPT_QUEUE_FULL] = "审批回复队列已满",
    [UI_TEXT_EVENT_PROMPT_EXPIRED] = "审批回复已过期",
    [UI_TEXT_EVENT_PROMPT_FAILED] = "审批回复失败",
    [UI_TEXT_EVENT_PROMPT_APPROVAL_QUEUED] = "允许回复已入队",
    [UI_TEXT_EVENT_PROMPT_DENIAL_QUEUED] = "拒绝回复已入队",
    [UI_TEXT_BLE_DEVICE_NAME] = "蓝牙设备名",
};

void ui_locale_init(void)
{
    nvs_handle_t nvs = 0;
    uint8_t stored = UI_LOCALE_ZH_CN;

    if (nvs_open(UI_LOCALE_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        (void)nvs_get_u8(nvs, UI_LOCALE_NVS_KEY, &stored);
        nvs_close(nvs);
    }
    if (stored >= UI_LOCALE_COUNT) {
        stored = UI_LOCALE_ZH_CN;
    }
    portENTER_CRITICAL(&s_locale_lock);
    s_locale = (ui_locale_t)stored;
    portEXIT_CRITICAL(&s_locale_lock);
}

ui_locale_t ui_locale_get(void)
{
    ui_locale_t locale;

    portENTER_CRITICAL(&s_locale_lock);
    locale = s_locale;
    portEXIT_CRITICAL(&s_locale_lock);
    return locale;
}

esp_err_t ui_locale_apply(ui_locale_t locale)
{
    if (locale < UI_LOCALE_ZH_CN || locale >= UI_LOCALE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_locale_lock);
    s_locale = locale;
    portEXIT_CRITICAL(&s_locale_lock);
    return ESP_OK;
}

esp_err_t ui_locale_save(ui_locale_t locale)
{
    nvs_handle_t nvs = 0;
    esp_err_t err;

    if (locale < UI_LOCALE_ZH_CN || locale >= UI_LOCALE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_open(UI_LOCALE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, UI_LOCALE_NVS_KEY, (uint8_t)locale);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (nvs != 0) {
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save locale failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ui_locale_set(ui_locale_t locale)
{
    return ui_locale_apply(locale);
}

void ui_locale_reset(void)
{
    portENTER_CRITICAL(&s_locale_lock);
    s_locale = UI_LOCALE_ZH_CN;
    portEXIT_CRITICAL(&s_locale_lock);
}

esp_err_t ui_locale_clear_saved(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(UI_LOCALE_NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (err == ESP_OK) {
        err = nvs_erase_all(nvs);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (nvs != 0) {
        nvs_close(nvs);
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

const char *ui_locale_code(ui_locale_t locale)
{
    return locale == UI_LOCALE_EN_US ? "en_US" : "zh_CN";
}

const char *ui_locale_weather_language(void)
{
    return ui_locale_get() == UI_LOCALE_ZH_CN ? "zh-Hans" : "en";
}

const char *ui_text_for(ui_locale_t locale, ui_text_id_t id)
{
    const char *text;

    if (id < 0 || id >= UI_TEXT_COUNT) {
        return "";
    }
    text = locale == UI_LOCALE_ZH_CN ? s_text_zh[id] : s_text_en[id];
    if (text == NULL) {
        text = s_text_en[id];
    }
    return text != NULL ? text : "";
}

const char *ui_text(ui_text_id_t id)
{
    return ui_text_for(ui_locale_get(), id);
}

void ui_locale_font_init(void)
{
#if !CONFIG_BUDDY_DYNAMIC_ZH_FONT
    ui_locale_prepare_fixed_fonts();
    ESP_LOGI(TAG, "Dynamic zh font: disabled");
    return;
#else
    if (s_dynamic_font != NULL) {
        return;
    }
    ESP_LOGI(TAG, "Dynamic zh font: enabled");
    s_dynamic_font = lv_binfont_create(UI_LOCALE_DYNAMIC_FONT_PATH);
    if (s_dynamic_font != NULL && !ui_locale_dynamic_font_valid(s_dynamic_font)) {
        lv_binfont_destroy(s_dynamic_font);
        s_dynamic_font = NULL;
        ESP_LOGW(TAG, "storage font is missing required v%d glyphs: %s",
                 UI_LOCALE_DYNAMIC_FONT_VERSION, UI_LOCALE_DYNAMIC_FONT_PATH);
    }
    if (s_dynamic_font == NULL) {
        /* 内存文件直接引用应用镜像，不改写 FAT 分区中的角色包和字库。 */
        s_dynamic_font = lv_binfont_create_from_buffer(
            (void *)_binary_ui_zh_cn_14_bin_start,
            (uint32_t)(_binary_ui_zh_cn_14_bin_end - _binary_ui_zh_cn_14_bin_start));
        if (s_dynamic_font != NULL && !ui_locale_dynamic_font_valid(s_dynamic_font)) {
            lv_binfont_destroy(s_dynamic_font);
            s_dynamic_font = NULL;
        }
        if (s_dynamic_font == NULL) {
            ESP_LOGE(TAG, "dynamic font unavailable in storage and firmware");
            ui_locale_prepare_fixed_fonts();
            return;
        }
        ESP_LOGW(TAG, "using firmware font fallback: 14px v%d",
                 UI_LOCALE_DYNAMIC_FONT_VERSION);
    } else {
        ESP_LOGI(TAG, "dynamic font loaded from storage: 14px v%d %s",
                 UI_LOCALE_DYNAMIC_FONT_VERSION, UI_LOCALE_DYNAMIC_FONT_PATH);
    }
    s_content_fallback = ui_font_zh_14;
    s_dynamic_font->fallback = &s_content_fallback;
    ui_locale_prepare_fixed_fonts();
#endif
}

const lv_font_t *ui_locale_font(const lv_font_t *latin_font)
{
    if (ui_locale_get() != UI_LOCALE_ZH_CN) {
        return latin_font;
    }
    return ui_locale_layered_font(latin_font);
}

const lv_font_t *ui_locale_static_font(const lv_font_t *latin_font)
{
    return ui_locale_get() == UI_LOCALE_ZH_CN ? ui_locale_layered_font(latin_font) : latin_font;
}

bool ui_locale_dynamic_font_available(void)
{
    return s_dynamic_font != NULL;
}

/* 审批是外部 UTF-8 文本，即使界面为英文也需要中文字符覆盖。 */
const lv_font_t *ui_locale_content_font(void)
{
    return s_dynamic_font != NULL ? s_dynamic_font : &ui_font_zh_14;
}
