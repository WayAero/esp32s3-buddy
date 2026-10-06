/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_STATUS_LED_MODE_OFF = 0,
    BSP_STATUS_LED_MODE_NORMAL,
    BSP_STATUS_LED_MODE_ATTENTION,
    BSP_STATUS_LED_MODE_ALERT,
} bsp_status_led_mode_t;

typedef struct {
    uint16_t raw_x_top;
    uint16_t raw_x_bottom;
    uint16_t raw_y_left;
    uint16_t raw_y_right;
} bsp_touch_calibration_t;

#define BSP_DISPLAY_DIAG_TASK_NAME_LEN 16

typedef enum {
    BSP_DISPLAY_DIAG_PHASE_IDLE = 0,
    BSP_DISPLAY_DIAG_PHASE_FLUSH_START,
    BSP_DISPLAY_DIAG_PHASE_FLUSH_FINISH,
    BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_START,
    BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_FINISH,
} bsp_display_diag_phase_t;

typedef enum {
    BSP_DISPLAY_SPI_OWNER_NONE = 0,
    BSP_DISPLAY_SPI_OWNER_LCD,
    BSP_DISPLAY_SPI_OWNER_TOUCH,
    BSP_DISPLAY_SPI_OWNER_ROTATION,
} bsp_display_spi_owner_t;

typedef struct {
    bsp_display_diag_phase_t flush_phase;
    int64_t flush_phase_time_us;
    uint32_t flush_start_count;
    uint32_t flush_finish_count;
    uint32_t flush_wait_start_count;
    uint32_t flush_wait_finish_count;
    uint32_t color_done_count;
    int64_t color_done_time_us;
    uint32_t touch_enter_count;
    uint32_t touch_exit_count;
    int64_t touch_enter_time_us;
    int64_t touch_exit_time_us;
    uint8_t touch_last_point_count;
    uint32_t touch_sample_enter_count;
    uint32_t touch_sample_exit_count;
    int64_t touch_sample_enter_time_us;
    int64_t touch_sample_exit_time_us;
    uint32_t touch_sample_error_count;
    uint32_t touch_sample_skip_count;
    uint8_t touch_sample_last_point_count;
    int touch_irq_level;
    bool touch_snapshot_pen_down;
    uint8_t touch_snapshot_count;
    int64_t touch_snapshot_time_us;
    uint32_t touch_notify_count;
    bsp_display_spi_owner_t spi_gate_owner;
    uint32_t spi_gate_take_count;
    uint32_t spi_gate_give_count;
    int64_t spi_gate_owner_since_us;
    uint32_t wrapper_lock_depth;
    int64_t wrapper_lock_since_us;
    char wrapper_lock_holder_name[BSP_DISPLAY_DIAG_TASK_NAME_LEN];
    int wrapper_lock_holder_core_id;
} bsp_display_diag_t;

lv_display_t *bsp_display_start(void);
void bsp_display_backlight_on(void);
esp_err_t bsp_display_set_brightness(uint8_t brightness_percent);
esp_err_t bsp_display_set_rotation(lv_display_rotation_t rotation);
esp_err_t bsp_display_set_rotation_locked(lv_display_rotation_t rotation);
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);
esp_err_t bsp_display_get_diag(bsp_display_diag_t *out_diag);
bool bsp_touch_calibration_is_valid(void);
esp_err_t bsp_touch_get_last_raw(uint16_t *raw_x, uint16_t *raw_y);
esp_err_t bsp_touch_set_calibration(const bsp_touch_calibration_t *calibration);
esp_err_t bsp_touch_reset_calibration(void);
esp_err_t bsp_status_led_start(void);
void bsp_status_led_set_mode(bsp_status_led_mode_t mode);
void bsp_status_led_set_attention(bool attention);
TaskHandle_t bsp_status_led_task_handle(void);

bool bsp_display_wait_flush_locked(uint32_t timeout_ms);
void bsp_memory_log_stage(const char *stage);

#ifdef __cplusplus
}
#endif
