/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bsp/esp-bsp.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "nvs.h"

#define LCD_HOST SPI2_HOST
#define LCD_SCLK_GPIO 12
#define LCD_MOSI_GPIO 11
#define LCD_MISO_GPIO 8
#define LCD_DC_GPIO 9
#define LCD_CS_GPIO 10
#define LCD_RST_GPIO 17
#define LCD_BACKLIGHT_GPIO 18
#define TOUCH_CS_GPIO 15
#define TOUCH_IRQ_GPIO 16

#define LCD_WIDTH 240
#define LCD_HEIGHT 320
#define LCD_SPI_HZ (80 * 1000 * 1000)
#define TOUCH_ADC_MAX 4096
#define TOUCH_CAL_NAMESPACE "touch_cal"
#define TOUCH_CAL_KEY "points"
#define TOUCH_CAL_VERSION 1U
#define TOUCH_CAL_MIN_SPAN 500U
#define TOUCH_STABLE_RAW_DELTA 300U
#define TOUCH_STABLE_GAP_US 120000LL
#define TOUCH_STABLE_SAMPLE_COUNT 2U

#define BACKLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_FREQ_HZ 5000
#define BACKLIGHT_LEDC_DUTY_BITS LEDC_TIMER_10_BIT
#define BACKLIGHT_LEDC_DUTY_MAX ((1U << 10) - 1)

#define STATUS_LED_GPIO 48
#define STATUS_LED_COUNT 1
#define STATUS_LED_RMT_RES_HZ (10 * 1000 * 1000)
#define STATUS_LED_STACK 2048
#define STATUS_LED_PRIORITY 1
#define LCD_DRAW_BUFFER_LINES 16
#define LCD_DRAW_BUFFER_BYTES (LCD_WIDTH * LCD_DRAW_BUFFER_LINES * sizeof(uint16_t))
#define TOUCH_SAMPLE_PERIOD_MS 20U
#define TOUCH_TASK_STACK_SIZE 3072U
#define TOUCH_TASK_PRIORITY 2U
#define LCD_SPI_GATE_TIMEOUT_MS 100U
#define LCD_FLUSH_DONE_TIMEOUT_MS 1000U

#define BUDDY_DISPLAY_CORE CONFIG_BUDDY_LVGL_CORE
#define STATUS_LED_CORE CONFIG_BUDDY_LVGL_CORE
/* Keep LVGL and every SPI2 submission on the same core. */
#define TOUCH_TASK_CORE BUDDY_DISPLAY_CORE

static const char *TAG = "buddy_bsp";
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static lv_display_t *s_display;
static TaskHandle_t s_touch_task;
static lv_indev_t *s_touch_indev;
static led_strip_handle_t s_status_led;
static TaskHandle_t s_status_led_task;
static volatile bsp_status_led_mode_t s_status_led_mode = BSP_STATUS_LED_MODE_NORMAL;
static bool s_backlight_initialized;
static bool s_spi_initialized;
static portMUX_TYPE s_touch_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_display_diag_lock = portMUX_INITIALIZER_UNLOCKED;
/*
 * SPI2 carries both the asynchronous ST7789 color transfer and XPT2046 polling
 * reads. The ESP-IDF panel IO bus lock only covers queue submission, so it
 * cannot serialize a touch read with an already queued LCD DMA transaction.
 */
static StaticSemaphore_t s_shared_spi_gate_storage;
static SemaphoreHandle_t s_shared_spi_gate;
static StaticSemaphore_t s_lcd_flush_done_storage;
static SemaphoreHandle_t s_lcd_flush_done;
static bool s_lcd_flush_owns_spi_gate;
static bsp_display_diag_t s_display_diag = {
    .flush_phase = BSP_DISPLAY_DIAG_PHASE_IDLE,
    .wrapper_lock_holder_core_id = -1,
};
static TaskHandle_t s_wrapper_lock_holder;
static bsp_touch_calibration_t s_touch_calibration = {
    .raw_x_top = 0,
    .raw_x_bottom = TOUCH_ADC_MAX - 1,
    .raw_y_left = 0,
    .raw_y_right = TOUCH_ADC_MAX - 1,
};
static uint16_t s_touch_last_raw_x;
static uint16_t s_touch_last_raw_y;
static bool s_touch_have_raw;
static bool s_touch_calibration_valid;
static uint16_t s_touch_filter_raw_x;
static uint16_t s_touch_filter_raw_y;
static uint8_t s_touch_filter_count;
static int64_t s_touch_filter_last_us;
static int64_t s_touch_sample_last_error_log_us;
typedef struct {
    esp_lcd_touch_point_data_t points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint8_t count;
    int64_t sample_time_us;
    bool pen_down;
} buddy_touch_snapshot_t;

static buddy_touch_snapshot_t s_touch_snapshot;

typedef struct {
    uint8_t version;
    bsp_touch_calibration_t calibration;
} buddy_bsp_touch_calibration_record_t;

static void buddy_bsp_display_diag_reset(void)
{
    portENTER_CRITICAL(&s_display_diag_lock);
    memset(&s_display_diag, 0, sizeof(s_display_diag));
    s_display_diag.flush_phase = BSP_DISPLAY_DIAG_PHASE_IDLE;
    s_display_diag.wrapper_lock_holder_core_id = -1;
    s_wrapper_lock_holder = NULL;
    portEXIT_CRITICAL(&s_display_diag_lock);
}

static void buddy_bsp_display_diag_spi_gate_taken(bsp_display_spi_owner_t owner)
{
    portENTER_CRITICAL(&s_display_diag_lock);
    s_display_diag.spi_gate_owner = owner;
    s_display_diag.spi_gate_take_count++;
    s_display_diag.spi_gate_owner_since_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_display_diag_lock);
}

static void buddy_bsp_display_diag_spi_gate_given(void)
{
    portENTER_CRITICAL(&s_display_diag_lock);
    s_display_diag.spi_gate_owner = BSP_DISPLAY_SPI_OWNER_NONE;
    s_display_diag.spi_gate_give_count++;
    s_display_diag.spi_gate_owner_since_us = 0;
    portEXIT_CRITICAL(&s_display_diag_lock);
}

static void buddy_bsp_display_event(lv_event_t *event)
{
    bsp_display_diag_phase_t phase;
    uint32_t *counter;

    switch (lv_event_get_code(event)) {
    case LV_EVENT_FLUSH_START:
        /* 清除上次刷新的完成信号，避免本次等待误用旧信号。 */
        configASSERT(s_lcd_flush_done != NULL);
        while (xSemaphoreTake(s_lcd_flush_done, 0) == pdTRUE) {
        }
        phase = BSP_DISPLAY_DIAG_PHASE_FLUSH_START;
        counter = &s_display_diag.flush_start_count;
        break;
    case LV_EVENT_FLUSH_FINISH:
        phase = BSP_DISPLAY_DIAG_PHASE_FLUSH_FINISH;
        counter = &s_display_diag.flush_finish_count;
        break;
    case LV_EVENT_FLUSH_WAIT_START:
        phase = BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_START;
        counter = &s_display_diag.flush_wait_start_count;
        break;
    case LV_EVENT_FLUSH_WAIT_FINISH:
        phase = BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_FINISH;
        counter = &s_display_diag.flush_wait_finish_count;
        break;
    default:
        return;
    }

    portENTER_CRITICAL(&s_display_diag_lock);
    s_display_diag.flush_phase = phase;
    s_display_diag.flush_phase_time_us = esp_timer_get_time();
    (*counter)++;
    portEXIT_CRITICAL(&s_display_diag_lock);
}

static void buddy_bsp_lcd_flush_wait(lv_display_t *display)
{
    (void)display;
    configASSERT(s_lcd_flush_done != NULL);

    if (xSemaphoreTake(s_lcd_flush_done, pdMS_TO_TICKS(LCD_FLUSH_DONE_TIMEOUT_MS)) == pdTRUE) {
        return;
    }

    bsp_display_diag_t diag = {0};
    (void)bsp_display_get_diag(&diag);
    const int64_t flush_age_ms = diag.flush_phase_time_us > 0
                                     ? (esp_timer_get_time() - diag.flush_phase_time_us) / 1000LL
                                     : -1;
    ESP_LOGE(TAG,
             "LCD DMA completion timeout phase=%d age=%" PRId64 "ms flush=%lu/%lu wait=%lu/%lu color_done=%lu",
             (int)diag.flush_phase, flush_age_ms,
             (unsigned long)diag.flush_start_count, (unsigned long)diag.flush_finish_count,
             (unsigned long)diag.flush_wait_start_count, (unsigned long)diag.flush_wait_finish_count,
             (unsigned long)diag.color_done_count);
    abort();
}

static esp_err_t buddy_bsp_lcd_draw_bitmap(lv_display_t *display,
                                           esp_lcd_panel_handle_t panel,
                                           int x_start, int y_start, int x_end, int y_end,
                                           const void *color_map, void *user_ctx)
{
    (void)display;
    (void)user_ctx;

    /* 仅在实际提交 LCD 传输时占用 SPI；适配器同步跳过刷新时不会进入这里。 */
    configASSERT(s_shared_spi_gate != NULL);
    if (xSemaphoreTake(s_shared_spi_gate, pdMS_TO_TICKS(LCD_SPI_GATE_TIMEOUT_MS)) != pdTRUE) {
        bsp_display_diag_t diag = {0};
        (void)bsp_display_get_diag(&diag);
        const int64_t owner_age_ms = diag.spi_gate_owner_since_us > 0
                                         ? (esp_timer_get_time() - diag.spi_gate_owner_since_us) / 1000LL
                                         : -1;
        ESP_LOGE(TAG, "LCD flush SPI gate timeout owner=%d held=%" PRId64 "ms takes=%lu gives=%lu",
                 (int)diag.spi_gate_owner, owner_age_ms,
                 (unsigned long)diag.spi_gate_take_count, (unsigned long)diag.spi_gate_give_count);
        abort();
    }
    buddy_bsp_display_diag_spi_gate_taken(BSP_DISPLAY_SPI_OWNER_LCD);
    portENTER_CRITICAL(&s_display_diag_lock);
    configASSERT(!s_lcd_flush_owns_spi_gate);
    s_lcd_flush_owns_spi_gate = true;
    portEXIT_CRITICAL(&s_display_diag_lock);

    const esp_err_t err = esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, color_map);
    if (err != ESP_OK) {
        /* 提交失败不会产生完成中断，由当前任务释放 SPI 门控。 */
        portENTER_CRITICAL(&s_display_diag_lock);
        const bool release_spi_gate = s_lcd_flush_owns_spi_gate;
        s_lcd_flush_owns_spi_gate = false;
        portEXIT_CRITICAL(&s_display_diag_lock);
        if (release_spi_gate) {
            buddy_bsp_display_diag_spi_gate_given();
            configASSERT(xSemaphoreGive(s_shared_spi_gate) == pdTRUE);
        }
    }
    return err;
}

static bool buddy_bsp_add_display_diag_event(lv_display_t *display, lv_event_code_t code)
{
    const uint32_t before = lv_display_get_event_count(display);
    lv_display_add_event_cb(display, buddy_bsp_display_event, code, NULL);
    if (lv_display_get_event_count(display) != before + 1U) {
        ESP_LOGE(TAG, "failed to register display diagnostic event code=%u", (unsigned)code);
        return false;
    }
    return true;
}

static esp_err_t buddy_bsp_touch_read_diag(esp_lcd_touch_handle_t touch,
                                           esp_lcd_touch_point_data_t *points,
                                           uint8_t *count,
                                           uint8_t max_count,
                                           void *user_ctx)
{
    (void)touch;
    (void)user_ctx;

    portENTER_CRITICAL(&s_display_diag_lock);
    s_display_diag.touch_enter_count++;
    s_display_diag.touch_enter_time_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_display_diag_lock);

    portENTER_CRITICAL(&s_touch_lock);
    *count = s_touch_snapshot.pen_down
                 ? (s_touch_snapshot.count > max_count ? max_count : s_touch_snapshot.count)
                 : 0;
    if (*count > 0) {
        memcpy(points, s_touch_snapshot.points, *count * sizeof(points[0]));
    }
    portEXIT_CRITICAL(&s_touch_lock);

    portENTER_CRITICAL(&s_display_diag_lock);
    s_display_diag.touch_exit_count++;
    s_display_diag.touch_exit_time_us = esp_timer_get_time();
    s_display_diag.touch_last_point_count = *count;
    portEXIT_CRITICAL(&s_display_diag_lock);

    return ESP_OK;
}

static void IRAM_ATTR buddy_bsp_touch_irq(esp_lcd_touch_handle_t touch)
{
    BaseType_t need_yield = pdFALSE;

    (void)touch;
    if (s_touch_task != NULL) {
        vTaskNotifyGiveFromISR(s_touch_task, &need_yield);
    }
    if (need_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void buddy_bsp_touch_snapshot_release(void)
{
    portENTER_CRITICAL(&s_touch_lock);
    s_touch_snapshot.count = 0;
    s_touch_snapshot.sample_time_us = esp_timer_get_time();
    s_touch_snapshot.pen_down = false;
    portEXIT_CRITICAL(&s_touch_lock);

    if (s_touch_indev != NULL) {
        (void)esp_lv_adapter_touch_notify_interrupt(s_touch_indev);
    }
}

static void buddy_bsp_touch_snapshot_publish(const esp_lcd_touch_point_data_t *points,
                                             uint8_t count,
                                             int64_t sample_time_us)
{
    portENTER_CRITICAL(&s_touch_lock);
    s_touch_snapshot.count = count;
    if (count > 0) {
        memcpy(s_touch_snapshot.points, points, count * sizeof(points[0]));
    }
    s_touch_snapshot.sample_time_us = sample_time_us;
    s_touch_snapshot.pen_down = count > 0;
    portEXIT_CRITICAL(&s_touch_lock);

    if (s_touch_indev != NULL) {
        (void)esp_lv_adapter_touch_notify_interrupt(s_touch_indev);
    }
}

static void buddy_bsp_touch_sample_task(void *arg)
{
    (void)arg;

    while (true) {
        const uint32_t notified = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&s_display_diag_lock);
        s_display_diag.touch_notify_count += notified;
        portEXIT_CRITICAL(&s_display_diag_lock);

        /* Raw calibration data must belong to the current press. */
        s_touch_filter_count = 0;
        s_touch_filter_last_us = 0;
        portENTER_CRITICAL(&s_touch_lock);
        s_touch_have_raw = false;
        portEXIT_CRITICAL(&s_touch_lock);

        while (gpio_get_level(TOUCH_IRQ_GPIO) == 0) {
            esp_lcd_touch_point_data_t points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
            uint8_t count = 0;
            const int64_t start_time_us = esp_timer_get_time();

            /* Never wait here: a skipped sample is safer than delaying an LCD flush. */
            if (xSemaphoreTake(s_shared_spi_gate, 0) != pdTRUE) {
                portENTER_CRITICAL(&s_display_diag_lock);
                s_display_diag.touch_sample_skip_count++;
                portEXIT_CRITICAL(&s_display_diag_lock);
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            buddy_bsp_display_diag_spi_gate_taken(BSP_DISPLAY_SPI_OWNER_TOUCH);

            portENTER_CRITICAL(&s_display_diag_lock);
            s_display_diag.touch_sample_enter_count++;
            s_display_diag.touch_sample_enter_time_us = start_time_us;
            portEXIT_CRITICAL(&s_display_diag_lock);

            esp_err_t ret = esp_lcd_touch_read_data(s_touch);
            if (ret == ESP_OK) {
                ret = esp_lcd_touch_get_data(s_touch, points, &count, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
            }
            buddy_bsp_display_diag_spi_gate_given();
            configASSERT(xSemaphoreGive(s_shared_spi_gate) == pdTRUE);

            const int64_t finish_time_us = esp_timer_get_time();
            /* A failed read while T_IRQ is low is a missed sample, not a release. */
            if (ret == ESP_OK && count > 0) {
                buddy_bsp_touch_snapshot_publish(points, count, finish_time_us);
            }

            portENTER_CRITICAL(&s_display_diag_lock);
            s_display_diag.touch_sample_exit_count++;
            s_display_diag.touch_sample_exit_time_us = finish_time_us;
            s_display_diag.touch_sample_last_point_count = (ret == ESP_OK) ? count : 0U;
            if (ret != ESP_OK) {
                s_display_diag.touch_sample_error_count++;
            }
            portEXIT_CRITICAL(&s_display_diag_lock);

            if (ret != ESP_OK && finish_time_us - s_touch_sample_last_error_log_us >= 1000000LL) {
                s_touch_sample_last_error_log_us = finish_time_us;
                ESP_LOGW(TAG, "touch sample failed: %s", esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(TOUCH_SAMPLE_PERIOD_MS));
        }
        buddy_bsp_touch_snapshot_release();
    }
}

static bool IRAM_ATTR buddy_bsp_lcd_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                                      esp_lcd_panel_io_event_data_t *event_data,
                                                      void *user_ctx)
{
    (void)panel_io;
    (void)event_data;

    BaseType_t need_yield = pdFALSE;
    bool release_spi_gate = false;

    portENTER_CRITICAL_ISR(&s_display_diag_lock);
    if (s_lcd_flush_owns_spi_gate) {
        s_lcd_flush_owns_spi_gate = false;
        release_spi_gate = true;
    }
    s_display_diag.color_done_count++;
    s_display_diag.color_done_time_us = esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&s_display_diag_lock);

    if (release_spi_gate && s_lcd_flush_done != NULL) {
        (void)xSemaphoreGiveFromISR(s_lcd_flush_done, &need_yield);
    }

    if (release_spi_gate && s_shared_spi_gate != NULL &&
        xSemaphoreGiveFromISR(s_shared_spi_gate, &need_yield) == pdTRUE) {
        portENTER_CRITICAL_ISR(&s_display_diag_lock);
        s_display_diag.spi_gate_owner = BSP_DISPLAY_SPI_OWNER_NONE;
        s_display_diag.spi_gate_give_count++;
        s_display_diag.spi_gate_owner_since_us = 0;
        portEXIT_CRITICAL_ISR(&s_display_diag_lock);
    }

    return esp_lv_adapter_display_notify_color_trans_done_from_isr((lv_display_t *)user_ctx) ||
           (need_yield == pdTRUE);
}

static void buddy_bsp_release_panel_and_touch(void)
{
    if (s_touch != NULL) {
        (void)esp_lcd_touch_register_interrupt_callback(s_touch, NULL);
    }
    if (s_touch_task != NULL) {
        vTaskDelete(s_touch_task);
        s_touch_task = NULL;
    }
    s_touch_indev = NULL;
    if (s_touch != NULL) {
        (void)esp_lcd_touch_del(s_touch);
        s_touch = NULL;
    }
    if (s_touch_io != NULL) {
        (void)esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = NULL;
    }
    if (s_panel != NULL) {
        (void)esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_panel_io != NULL) {
        (void)esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
    }
    if (s_spi_initialized) {
        (void)spi_bus_free(LCD_HOST);
        s_spi_initialized = false;
    }
}

static bool buddy_bsp_touch_calibration_validate(const bsp_touch_calibration_t *calibration)
{
    if (calibration == NULL || calibration->raw_x_top >= TOUCH_ADC_MAX ||
        calibration->raw_x_bottom >= TOUCH_ADC_MAX ||
        calibration->raw_y_left >= TOUCH_ADC_MAX ||
        calibration->raw_y_right >= TOUCH_ADC_MAX) {
        return false;
    }
    const uint16_t x_span = calibration->raw_x_top > calibration->raw_x_bottom ?
                            calibration->raw_x_top - calibration->raw_x_bottom :
                            calibration->raw_x_bottom - calibration->raw_x_top;
    const uint16_t y_span = calibration->raw_y_left > calibration->raw_y_right ?
                            calibration->raw_y_left - calibration->raw_y_right :
                            calibration->raw_y_right - calibration->raw_y_left;
    return x_span >= TOUCH_CAL_MIN_SPAN && y_span >= TOUCH_CAL_MIN_SPAN;
}

static uint16_t buddy_bsp_touch_raw_to_coord(uint16_t raw,
                                              uint16_t start,
                                              uint16_t end,
                                              uint16_t size)
{
    const int32_t span = (int32_t)end - (int32_t)start;
    int32_t coord = ((int32_t)raw - (int32_t)start) * ((int32_t)size - 1) / span;

    if (coord < 0) {
        coord = 0;
    } else if (coord >= size) {
        coord = size - 1;
    }
    return (uint16_t)coord;
}

static void buddy_bsp_touch_calibration_load(void)
{
    buddy_bsp_touch_calibration_record_t record = {0};
    nvs_handle_t nvs = 0;
    size_t size = sizeof(record);

    if (nvs_open(TOUCH_CAL_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    const esp_err_t err = nvs_get_blob(nvs, TOUCH_CAL_KEY, &record, &size);
    nvs_close(nvs);
    if (err == ESP_OK && size == sizeof(record) && record.version == TOUCH_CAL_VERSION &&
        buddy_bsp_touch_calibration_validate(&record.calibration)) {
        s_touch_calibration = record.calibration;
        s_touch_calibration_valid = true;
        ESP_LOGI(TAG, "touch calibration loaded");
    }
}

static void buddy_bsp_touch_process_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                                                 uint16_t *strength, uint8_t *point_num,
                                                 uint8_t max_point_num)
{
    (void)tp;
    (void)strength;
    (void)max_point_num;
    if (*point_num == 0) {
        return;
    }

    const uint16_t raw_x = x[0];
    const uint16_t raw_y = y[0];
    const int64_t now_us = esp_timer_get_time();
    const uint16_t delta_x = raw_x > s_touch_filter_raw_x ?
                             raw_x - s_touch_filter_raw_x : s_touch_filter_raw_x - raw_x;
    const uint16_t delta_y = raw_y > s_touch_filter_raw_y ?
                             raw_y - s_touch_filter_raw_y : s_touch_filter_raw_y - raw_y;
    bsp_touch_calibration_t calibration;

    if (s_touch_filter_last_us == 0 ||
        now_us - s_touch_filter_last_us > TOUCH_STABLE_GAP_US ||
        delta_x > TOUCH_STABLE_RAW_DELTA || delta_y > TOUCH_STABLE_RAW_DELTA) {
        s_touch_filter_count = 1;
    } else if (s_touch_filter_count < TOUCH_STABLE_SAMPLE_COUNT) {
        s_touch_filter_count++;
    }
    s_touch_filter_raw_x = raw_x;
    s_touch_filter_raw_y = raw_y;
    s_touch_filter_last_us = now_us;
    portENTER_CRITICAL(&s_touch_lock);
    if (s_touch_filter_count >= TOUCH_STABLE_SAMPLE_COUNT) {
        s_touch_last_raw_x = raw_x;
        s_touch_last_raw_y = raw_y;
        s_touch_have_raw = true;
    }
    calibration = s_touch_calibration;
    portEXIT_CRITICAL(&s_touch_lock);
    x[0] = buddy_bsp_touch_raw_to_coord(raw_y,
                                        calibration.raw_y_left,
                                        calibration.raw_y_right,
                                        LCD_WIDTH);
    y[0] = buddy_bsp_touch_raw_to_coord(raw_x,
                                        calibration.raw_x_top,
                                        calibration.raw_x_bottom,
                                        LCD_HEIGHT);
}

static esp_err_t buddy_bsp_init_backlight(void)
{
    if (s_backlight_initialized) {
        return ESP_OK;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_LEDC_DUTY_BITS,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "configure backlight timer");

    const ledc_channel_config_t channel = {
        .gpio_num = LCD_BACKLIGHT_GPIO,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "configure backlight channel");
    s_backlight_initialized = true;
    return ESP_OK;
}

static esp_err_t buddy_bsp_init_panel_and_touch(void)
{
    esp_err_t ret = ESP_FAIL;
    buddy_bsp_touch_calibration_load();
    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = LCD_MISO_GPIO,
        .sclk_io_num = LCD_SCLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_DRAW_BUFFER_BYTES,
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                      fail, TAG, "init SPI bus");
    s_spi_initialized = true;

    const esp_lcd_panel_io_spi_config_t lcd_io_config = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = LCD_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = LCD_SPI_HZ,
        .trans_queue_depth = 2,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                               &lcd_io_config, &s_panel_io),
                      fail, TAG, "create LCD IO");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel), fail, TAG, "create ST7789V panel");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(s_panel), fail, TAG, "reset panel");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(s_panel), fail, TAG, "init panel");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), fail, TAG, "set panel gap");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), fail, TAG, "set panel axes");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), fail, TAG, "set panel mirror");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), fail, TAG, "display on");

    esp_lcd_panel_io_spi_config_t touch_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_CS_GPIO);
    touch_io_config.pclk_hz = 1000 * 1000;
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                               &touch_io_config, &s_touch_io),
                      fail, TAG, "create touch IO");

    const esp_lcd_touch_config_t touch_config = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = TOUCH_IRQ_GPIO,
        .levels = {
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .process_coordinates = buddy_bsp_touch_process_coordinates,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(s_touch_io, &touch_config, &s_touch),
                      fail, TAG, "create XPT2046 touch");
    ESP_GOTO_ON_ERROR(buddy_bsp_init_backlight(), fail, TAG, "init backlight");
    return ESP_OK;

fail:
    buddy_bsp_release_panel_and_touch();
    return ret;
}

static esp_err_t buddy_bsp_apply_rotation(lv_display_rotation_t rotation)
{
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        swap_xy = false; mirror_x = false; mirror_y = false;
        break;
    case LV_DISPLAY_ROTATION_90:
        swap_xy = true; mirror_x = false; mirror_y = true;
        break;
    case LV_DISPLAY_ROTATION_180:
        swap_xy = false; mirror_x = true; mirror_y = true;
        break;
    case LV_DISPLAY_ROTATION_270:
        swap_xy = true; mirror_x = true; mirror_y = false;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    configASSERT(s_shared_spi_gate != NULL);
    if (xSemaphoreTake(s_shared_spi_gate, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    buddy_bsp_display_diag_spi_gate_taken(BSP_DISPLAY_SPI_OWNER_ROTATION);
    const esp_err_t swap_err = esp_lcd_panel_swap_xy(s_panel, swap_xy);
    const esp_err_t mirror_err = swap_err == ESP_OK ? esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y) : swap_err;
    buddy_bsp_display_diag_spi_gate_given();
    configASSERT(xSemaphoreGive(s_shared_spi_gate) == pdTRUE);
    ESP_RETURN_ON_ERROR(swap_err, TAG, "rotate panel axes");
    ESP_RETURN_ON_ERROR(mirror_err, TAG, "rotate panel mirror");
    /* LVGL rotates pointer coordinates for the input device associated with this display. */
    lv_display_set_rotation(s_display, rotation);
    return ESP_OK;
}

static void buddy_bsp_status_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (s_status_led != NULL) {
        led_strip_set_pixel(s_status_led, 0, red, green, blue);
        led_strip_refresh(s_status_led);
    }
}

static void buddy_bsp_status_led_task(void *arg)
{
    (void)arg;
    bool on = false;
    while (true) {
        const bsp_status_led_mode_t mode = s_status_led_mode;
        TickType_t delay_ticks = pdMS_TO_TICKS(700);
        if (mode == BSP_STATUS_LED_MODE_OFF) {
            led_strip_clear(s_status_led);
        } else if (on) {
            if (mode == BSP_STATUS_LED_MODE_ALERT) {
                buddy_bsp_status_led_set_rgb(48, 0, 0); delay_ticks = pdMS_TO_TICKS(90);
            } else if (mode == BSP_STATUS_LED_MODE_ATTENTION) {
                buddy_bsp_status_led_set_rgb(32, 20, 0); delay_ticks = pdMS_TO_TICKS(180);
            } else {
                buddy_bsp_status_led_set_rgb(10, 10, 10);
            }
        } else {
            led_strip_clear(s_status_led);
            delay_ticks = pdMS_TO_TICKS(mode == BSP_STATUS_LED_MODE_ALERT ? 90 :
                                        mode == BSP_STATUS_LED_MODE_ATTENTION ? 180 : 700);
        }
        on = !on;
        vTaskDelay(delay_ticks);
    }
}

lv_display_t *bsp_display_start(void)
{
    lv_display_t *display;
    lv_indev_t *touch_indev;

    if (s_display != NULL) {
        return s_display;
    }
    if (buddy_bsp_init_panel_and_touch() != ESP_OK) {
        return NULL;
    }
    bsp_memory_log_stage("display start");
    buddy_bsp_display_diag_reset();
    s_lcd_flush_owns_spi_gate = false;
    s_lcd_flush_done = xSemaphoreCreateBinaryStatic(&s_lcd_flush_done_storage);
    s_shared_spi_gate = xSemaphoreCreateBinaryStatic(&s_shared_spi_gate_storage);
    if (s_lcd_flush_done == NULL || s_shared_spi_gate == NULL ||
        xSemaphoreGive(s_shared_spi_gate) != pdTRUE) {
        ESP_LOGE(TAG, "create display synchronization primitives failed");
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_core_id = BUDDY_DISPLAY_CORE;
    if (esp_lv_adapter_init(&adapter_config) != ESP_OK) {
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    bsp_memory_log_stage("LVGL start");
    if (esp_lv_adapter_touch_set_default_idf_interrupt_callback_registration_enabled(false) != ESP_OK) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    /* Own the SPI completion callback so its handoff to LVGL is explicit. */
    if (esp_lv_adapter_set_default_display_idf_callback_registration_enabled(false) != ESP_OK) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    esp_lv_adapter_display_config_t display_config =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(s_panel, s_panel_io,
                                                              LCD_WIDTH, LCD_HEIGHT,
                                                              ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = LCD_DRAW_BUFFER_LINES;
    display_config.profile.use_psram = false;
    display_config.profile.require_double_buffer = false;
    display = esp_lv_adapter_register_display(&display_config);
    if (display == NULL) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);
    const esp_lv_adapter_draw_bitmap_callbacks_t draw_callbacks = {
        .custom_draw_bitmap = buddy_bsp_lcd_draw_bitmap,
    };
    if (esp_lv_adapter_set_draw_bitmap_callbacks(display, &draw_callbacks, NULL) != ESP_OK) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    lv_display_set_flush_wait_cb(display, buddy_bsp_lcd_flush_wait);
    const bool flush_start_registered =
        buddy_bsp_add_display_diag_event(display, LV_EVENT_FLUSH_START);
    const bool flush_finish_registered =
        buddy_bsp_add_display_diag_event(display, LV_EVENT_FLUSH_FINISH);
    const bool flush_wait_start_registered =
        buddy_bsp_add_display_diag_event(display, LV_EVENT_FLUSH_WAIT_START);
    const bool flush_wait_finish_registered =
        buddy_bsp_add_display_diag_event(display, LV_EVENT_FLUSH_WAIT_FINISH);
    if (!flush_start_registered || !flush_finish_registered ||
        !flush_wait_start_registered || !flush_wait_finish_registered) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    const esp_lcd_panel_io_callbacks_t lcd_callbacks = {
        .on_color_trans_done = buddy_bsp_lcd_color_trans_done,
    };
    if (esp_lcd_panel_io_register_event_callbacks(s_panel_io, &lcd_callbacks, display) != ESP_OK) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    esp_lv_adapter_touch_config_t touch_config =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, s_touch);
    touch_config.callbacks.custom_touch_read = buddy_bsp_touch_read_diag;
    touch_indev = esp_lv_adapter_register_touch(&touch_config);
    if (touch_indev == NULL) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    s_touch_indev = touch_indev;
    if (esp_lv_adapter_start() != ESP_OK) {
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    ESP_LOGI(TAG, "LVGL scheduling: worker_core=%d swdraw_core=%d ui_core=%d",
             CONFIG_BUDDY_LVGL_CORE, CONFIG_BUDDY_LVGL_CORE, CONFIG_BUDDY_LVGL_CORE);
    s_touch_task = NULL;
    if (xTaskCreatePinnedToCore(buddy_bsp_touch_sample_task, "touch", TOUCH_TASK_STACK_SIZE,
                                NULL, TOUCH_TASK_PRIORITY, &s_touch_task, TOUCH_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "create touch sample task failed");
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    if (esp_lcd_touch_register_interrupt_callback(s_touch, buddy_bsp_touch_irq) != ESP_OK) {
        ESP_LOGE(TAG, "register touch IRQ callback failed");
        (void)esp_lv_adapter_deinit();
        buddy_bsp_release_panel_and_touch();
        return NULL;
    }
    if (gpio_get_level(TOUCH_IRQ_GPIO) == 0) {
        xTaskNotifyGive(s_touch_task);
    }
    s_display = display;
    ESP_LOGI(TAG, "ST7789V %dx%d SPI=%dMHz DMA buffer=%d lines and XPT2046 initialized",
             LCD_WIDTH, LCD_HEIGHT, LCD_SPI_HZ / 1000000, LCD_DRAW_BUFFER_LINES);
    return s_display;
}

esp_err_t bsp_display_set_brightness(uint8_t brightness_percent)
{
    ESP_RETURN_ON_FALSE(brightness_percent <= 100, ESP_ERR_INVALID_ARG, TAG, "brightness exceeds 100%%");
    ESP_RETURN_ON_ERROR(buddy_bsp_init_backlight(), TAG, "init backlight");
    const uint32_t duty = BACKLIGHT_LEDC_DUTY_MAX * brightness_percent / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty), TAG, "set backlight duty");
    return ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
}

void bsp_display_backlight_on(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_set_brightness(100));
}

esp_err_t bsp_display_set_rotation(lv_display_rotation_t rotation)
{
    ESP_RETURN_ON_FALSE(s_display != NULL && s_panel != NULL && s_touch != NULL,
                        ESP_ERR_INVALID_STATE, TAG, "display is not started");
    ESP_RETURN_ON_FALSE(bsp_display_lock(1000), ESP_ERR_TIMEOUT, TAG, "lock display");
    const esp_err_t err = buddy_bsp_apply_rotation(rotation);
    bsp_display_unlock();
    return err;
}

esp_err_t bsp_display_set_rotation_locked(lv_display_rotation_t rotation)
{
    ESP_RETURN_ON_FALSE(s_display != NULL && s_panel != NULL && s_touch != NULL,
                        ESP_ERR_INVALID_STATE, TAG, "display is not started");
    return buddy_bsp_apply_rotation(rotation);
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    if (esp_lv_adapter_lock((int32_t)timeout_ms) != ESP_OK) {
        return false;
    }

    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_display_diag_lock);
    if (s_wrapper_lock_holder == current) {
        s_display_diag.wrapper_lock_depth++;
    } else {
        s_wrapper_lock_holder = current;
        s_display_diag.wrapper_lock_depth = 1U;
        s_display_diag.wrapper_lock_since_us = esp_timer_get_time();
        s_display_diag.wrapper_lock_holder_core_id = (int)xTaskGetCoreID(current);
        const char *name = pcTaskGetName(current);
        if (name != NULL) {
            strncpy(s_display_diag.wrapper_lock_holder_name, name,
                    sizeof(s_display_diag.wrapper_lock_holder_name) - 1U);
            s_display_diag.wrapper_lock_holder_name[
                sizeof(s_display_diag.wrapper_lock_holder_name) - 1U] = '\0';
        } else {
            s_display_diag.wrapper_lock_holder_name[0] = '\0';
        }
    }
    portEXIT_CRITICAL(&s_display_diag_lock);
    return true;
}

void bsp_display_unlock(void)
{
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_display_diag_lock);
    if (s_wrapper_lock_holder == current && s_display_diag.wrapper_lock_depth > 0U) {
        s_display_diag.wrapper_lock_depth--;
        if (s_display_diag.wrapper_lock_depth == 0U) {
            s_wrapper_lock_holder = NULL;
            s_display_diag.wrapper_lock_since_us = 0;
            s_display_diag.wrapper_lock_holder_name[0] = '\0';
            s_display_diag.wrapper_lock_holder_core_id = -1;
        }
    }
    portEXIT_CRITICAL(&s_display_diag_lock);
    esp_lv_adapter_unlock();
}

esp_err_t bsp_display_get_diag(bsp_display_diag_t *out_diag)
{
    ESP_RETURN_ON_FALSE(out_diag != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid display diag output");

    portENTER_CRITICAL(&s_display_diag_lock);
    *out_diag = s_display_diag;
    portEXIT_CRITICAL(&s_display_diag_lock);
    portENTER_CRITICAL(&s_touch_lock);
    out_diag->touch_snapshot_pen_down = s_touch_snapshot.pen_down;
    out_diag->touch_snapshot_count = s_touch_snapshot.count;
    out_diag->touch_snapshot_time_us = s_touch_snapshot.sample_time_us;
    portEXIT_CRITICAL(&s_touch_lock);
    out_diag->touch_irq_level = s_touch != NULL ? gpio_get_level(TOUCH_IRQ_GPIO) : -1;
    return ESP_OK;
}

bool bsp_touch_calibration_is_valid(void)
{
    bool valid;

    portENTER_CRITICAL(&s_touch_lock);
    valid = s_touch_calibration_valid;
    portEXIT_CRITICAL(&s_touch_lock);
    return valid;
}

esp_err_t bsp_touch_get_last_raw(uint16_t *raw_x, uint16_t *raw_y)
{
    bool have_raw;

    ESP_RETURN_ON_FALSE(raw_x != NULL && raw_y != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "raw output is NULL");
    portENTER_CRITICAL(&s_touch_lock);
    have_raw = s_touch_have_raw;
    *raw_x = s_touch_last_raw_x;
    *raw_y = s_touch_last_raw_y;
    portEXIT_CRITICAL(&s_touch_lock);
    return have_raw ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t bsp_touch_set_calibration(const bsp_touch_calibration_t *calibration)
{
    buddy_bsp_touch_calibration_record_t record;
    nvs_handle_t nvs = 0;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(buddy_bsp_touch_calibration_validate(calibration),
                        ESP_ERR_INVALID_ARG, TAG, "invalid touch calibration");
    record = (buddy_bsp_touch_calibration_record_t){
        .version = TOUCH_CAL_VERSION,
        .calibration = *calibration,
    };
    err = nvs_open(TOUCH_CAL_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, TOUCH_CAL_KEY, &record, sizeof(record));
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_touch_lock);
        s_touch_calibration = *calibration;
        s_touch_calibration_valid = true;
        portEXIT_CRITICAL(&s_touch_lock);
    }
    if (nvs != 0) {
        nvs_close(nvs);
    }
    return err;
}

esp_err_t bsp_touch_reset_calibration(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(TOUCH_CAL_NAMESPACE, NVS_READWRITE, &nvs);

    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, TOUCH_CAL_KEY);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_touch_lock);
        s_touch_calibration = (bsp_touch_calibration_t){
            .raw_x_top = 0,
            .raw_x_bottom = TOUCH_ADC_MAX - 1,
            .raw_y_left = 0,
            .raw_y_right = TOUCH_ADC_MAX - 1,
        };
        s_touch_calibration_valid = false;
        portEXIT_CRITICAL(&s_touch_lock);
    }
    return err;
}

esp_err_t bsp_status_led_start(void)
{
    if (s_status_led_task != NULL) {
        return ESP_OK;
    }
    if (s_status_led == NULL) {
        const led_strip_config_t strip_config = {
            .strip_gpio_num = STATUS_LED_GPIO,
            .max_leds = STATUS_LED_COUNT,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        };
        const led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = STATUS_LED_RMT_RES_HZ,
        };
        ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_status_led), TAG, "status LED init");
        ESP_RETURN_ON_ERROR(led_strip_clear(s_status_led), TAG, "status LED clear");
    }
    if (xTaskCreatePinnedToCore(buddy_bsp_status_led_task, "status_led", STATUS_LED_STACK, NULL,
                                STATUS_LED_PRIORITY, &s_status_led_task, STATUS_LED_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void bsp_status_led_set_attention(bool attention)
{
    bsp_status_led_set_mode(attention ? BSP_STATUS_LED_MODE_ATTENTION : BSP_STATUS_LED_MODE_NORMAL);
}

void bsp_status_led_set_mode(bsp_status_led_mode_t mode)
{
    s_status_led_mode = mode;
}

TaskHandle_t bsp_status_led_task_handle(void)
{
    return s_status_led_task;
}

bool bsp_display_wait_flush_locked(uint32_t timeout_ms)
{
    /* 调用者持有 LVGL 锁，禁止新刷新；最后一次 DMA 完成后 ISR 归还门控。 */
    if (s_shared_spi_gate == NULL ||
        xSemaphoreTake(s_shared_spi_gate, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return false;
    return xSemaphoreGive(s_shared_spi_gate) == pdTRUE;
}

void bsp_memory_log_stage(const char *stage)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    ESP_LOGI(TAG, "memory stage=%s internal=%u largest=%u min=%u dma=%u largest=%u min=%u psram=%u",
             stage, (unsigned)heap_caps_get_free_size(internal),
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)heap_caps_get_minimum_free_size(internal),
             (unsigned)heap_caps_get_free_size(dma),
             (unsigned)heap_caps_get_largest_free_block(dma),
             (unsigned)heap_caps_get_minimum_free_size(dma),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
