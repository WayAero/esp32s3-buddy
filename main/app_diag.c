/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_shared.h"

#define BUDDY_APP_DIAG_SNAPSHOT_TIMEOUT_MS 20

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "freertos/task.h"

typedef struct {
    esp_err_t migration_result;
    int32_t tz_offset_seconds;
    uint32_t aod_timeout_minutes;
    buddy_app_time_source_t time_source;
    int64_t last_synced_seconds;
    esp_desktop_buddy_t *buddy;
    esp_desktop_buddy_transport_ble_t *transport;
    example_charpack_t *charpack;
    esp_desktop_buddy_folder_push_t *folder_push;
    TaskHandle_t task_handles[BUDDY_APP_DIAG_TASK_COUNT];
} buddy_app_diag_app_state_t;

static uint32_t buddy_app_diag_stack_watermark(TaskHandle_t task)
{
    return task == NULL ? 0U : (uint32_t)uxTaskGetStackHighWaterMark(task);
}

static const char *buddy_app_diag_display_phase_name(bsp_display_diag_phase_t phase)
{
    switch (phase) {
    case BSP_DISPLAY_DIAG_PHASE_IDLE:
        return "idle";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_START:
        return "flush_start";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_FINISH:
        return "flush_finish";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_START:
        return "flush_wait_start";
    case BSP_DISPLAY_DIAG_PHASE_FLUSH_WAIT_FINISH:
        return "flush_wait_finish";
    default:
        return "unknown";
    }
}

static const char *buddy_app_diag_spi_owner_name(bsp_display_spi_owner_t owner)
{
    switch (owner) {
    case BSP_DISPLAY_SPI_OWNER_NONE:
        return "none";
    case BSP_DISPLAY_SPI_OWNER_LCD:
        return "lcd";
    case BSP_DISPLAY_SPI_OWNER_TOUCH:
        return "touch";
    case BSP_DISPLAY_SPI_OWNER_ROTATION:
        return "rotation";
    default:
        return "unknown";
    }
}

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static const char *buddy_app_diag_task_affinity(TaskHandle_t task)
{
    const BaseType_t core_id = xTaskGetCoreID(task);

    if (core_id == tskNO_AFFINITY) {
        return "any";
    }
    return core_id == 0 ? "0" : "1";
}

static void buddy_app_diag_print_runtime(FILE *out)
{
    const UBaseType_t task_capacity = uxTaskGetNumberOfTasks() + 4U;
    TaskStatus_t *tasks;
    uint32_t total_runtime = 0;
    UBaseType_t task_count;

    tasks = calloc(task_capacity, sizeof(*tasks));
    if (tasks == NULL) {
        fprintf(out, "runtime unavailable: no memory\n");
        return;
    }

    task_count = uxTaskGetSystemState(tasks, task_capacity, &total_runtime);
    fprintf(out,
            "runtime total=%lu idle core0=%lu%% core1=%lu%%\n",
            (unsigned long)total_runtime,
            (unsigned long)ulTaskGetIdleRunTimePercentForCore(0),
            (unsigned long)ulTaskGetIdleRunTimePercentForCore(1));
    for (UBaseType_t i = 0; i < task_count; ++i) {
        const uint64_t percent_hundredths = total_runtime == 0U ? 0U :
            ((uint64_t)tasks[i].ulRunTimeCounter * 10000U) / total_runtime;

        fprintf(out,
                "  %-16s affinity=%-3s prio=%lu watermark=%lu bytes runtime=%lu %lu.%02lu%%\n",
                tasks[i].pcTaskName,
                buddy_app_diag_task_affinity(tasks[i].xHandle),
                (unsigned long)tasks[i].uxCurrentPriority,
                (unsigned long)tasks[i].usStackHighWaterMark,
                (unsigned long)tasks[i].ulRunTimeCounter,
                (unsigned long)(percent_hundredths / 100U),
                (unsigned long)(percent_hundredths % 100U));
    }
    free(tasks);
}
#endif
static void buddy_app_diag_describe_service_errors(buddy_app_diag_snapshot_t *snapshot,
                                                   const buddy_app_diag_app_state_t *state)
{
    if (state->migration_result != ESP_OK)
        snprintf(snapshot->service_error, sizeof(snapshot->service_error), "migration=%s", esp_err_to_name(state->migration_result));
    else if (state->charpack == NULL)
        strlcpy(snapshot->service_error, "storage_unavailable", sizeof(snapshot->service_error));
}


static void buddy_app_diag_collect(buddy_app_t *app,
                                   buddy_app_diag_snapshot_t *snapshot,
                                   buddy_app_diag_app_state_t *state)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    buddy_app_diag_app_state_t local_state = {0};

    if (snapshot == NULL) {
        return;
    }
    *snapshot = (buddy_app_diag_snapshot_t){0};
    if (app == NULL || app->mutex == NULL) {
        return;
    }

    /* Heap queries and app metadata never run while the shared-state mutex is held. */
    snapshot->internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snapshot->internal_min_free = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    snapshot->internal_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    snapshot->dma_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DMA);
    snapshot->dma_min_free = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
    snapshot->dma_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    snapshot->psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snapshot->psram_min_free = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    snapshot->psram_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (app_desc != NULL) {
        const char *git = strstr(app_desc->version, "-g");

        strlcpy(snapshot->version, app_desc->version, sizeof(snapshot->version));
        if (git != NULL) {
            size_t git_len;

            git += 2;
            git_len = strcspn(git, "-");
            if (git_len >= sizeof(snapshot->git_commit)) {
                git_len = sizeof(snapshot->git_commit) - 1U;
            }
            memcpy(snapshot->git_commit, git, git_len);
            snapshot->git_commit[git_len] = '\0';
        }
        strlcpy(snapshot->idf_version, app_desc->idf_ver, sizeof(snapshot->idf_version));
        snprintf(snapshot->build,
                 sizeof(snapshot->build),
                 "%s %s",
                 app_desc->date,
                 app_desc->time);
    } else {
        strlcpy(snapshot->idf_version, esp_get_idf_version(), sizeof(snapshot->idf_version));
    }
    local_state.task_handles[BUDDY_APP_DIAG_TASK_STATUS_LED] = bsp_status_led_task_handle();

    if (xSemaphoreTake(app->mutex,
                       pdMS_TO_TICKS(BUDDY_APP_DIAG_SNAPSHOT_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    local_state.tz_offset_seconds = app->tz_offset_seconds;
    local_state.aod_timeout_minutes = app->aod_timeout_minutes;
    local_state.time_source = app->time_source;
    local_state.migration_result = app->settings_migration_result;
    local_state.last_synced_seconds = app->time_last_synced_seconds;
    local_state.buddy = app->buddy;
    local_state.transport = app->transport;
    local_state.charpack = app->charpack;
    local_state.folder_push = app->folder_push;
    snapshot->activity_count = app->activity_count;
    snapshot->prompt_reply_dropped_count = app->prompt_reply_dropped_count;
    snapshot->prompt_reply_failed_count = app->prompt_reply_failed_count;
    local_state.task_handles[BUDDY_APP_DIAG_TASK_UI] = app->ui_task;
    local_state.task_handles[BUDDY_APP_DIAG_TASK_SETTINGS] = app->settings_task;
    xSemaphoreGive(app->mutex);

    for (size_t i = 0; i < BUDDY_APP_DIAG_TASK_COUNT; ++i) {
        snapshot->task_stack_bytes[i] = buddy_app_diag_stack_watermark(local_state.task_handles[i]);
    }
    buddy_app_diag_describe_service_errors(snapshot, &local_state);
    if (state != NULL) {
        *state = local_state;
    }
}

void buddy_app_diag_snapshot_get(buddy_app_t *app, buddy_app_diag_snapshot_t *snapshot)
{
    buddy_app_diag_collect(app, snapshot, NULL);
}

void buddy_app_diag_print(buddy_app_t *app, FILE *out)
{
    buddy_app_diag_snapshot_t snapshot;
    buddy_app_diag_app_state_t state;
    bsp_display_diag_t display_diag;
    esp_desktop_buddy_rx_diagnostics_t rx_diagnostics = {0};
    esp_desktop_buddy_transport_ble_diagnostics_t transport_diagnostics = {0};
    example_charpack_diagnostics_t charpack_diagnostics = {0};
    esp_desktop_buddy_folder_push_diagnostics_t folder_push_diagnostics = {0};
    static const char *const task_names[BUDDY_APP_DIAG_TASK_COUNT] = {
        [BUDDY_APP_DIAG_TASK_UI] = "ui",
        [BUDDY_APP_DIAG_TASK_SETTINGS] = "settings",
        [BUDDY_APP_DIAG_TASK_STATUS_LED] = "status_led",
    };

    if (app == NULL || out == NULL) {
        return;
    }
    buddy_app_diag_collect(app, &snapshot, &state);
    if (state.buddy != NULL) {
        (void)esp_desktop_buddy_get_rx_diagnostics(state.buddy, &rx_diagnostics);
    }
    if (state.transport != NULL) {
        (void)esp_desktop_buddy_transport_ble_get_diagnostics(state.transport, &transport_diagnostics);
    }
    if (state.charpack != NULL) {
        (void)example_charpack_get_diagnostics(state.charpack, &charpack_diagnostics);
    }
    if (state.folder_push != NULL) {
        (void)esp_desktop_buddy_folder_push_get_diagnostics(state.folder_push,
                                                             &folder_push_diagnostics);
    }

    fprintf(out, "diag\n");
    fprintf(out,
            "build version=%s id=%s idf=%s time=%s\n",
            snapshot.version[0] ? snapshot.version : "<none>",
            snapshot.git_commit[0] ? snapshot.git_commit : "<none>",
            snapshot.idf_version[0] ? snapshot.idf_version : "<none>",
            snapshot.build[0] ? snapshot.build : "<none>");
    fprintf(out,
            "heap internal=%lu min=%lu largest=%lu dma=%lu dma_min=%lu dma_largest=%lu "
            "psram=%lu psram_min=%lu psram_largest=%lu\n",
            (unsigned long)snapshot.internal_free,
            (unsigned long)snapshot.internal_min_free,
            (unsigned long)snapshot.internal_largest,
            (unsigned long)snapshot.dma_free,
            (unsigned long)snapshot.dma_min_free,
            (unsigned long)snapshot.dma_largest,
            (unsigned long)snapshot.psram_free,
            (unsigned long)snapshot.psram_min_free,
            (unsigned long)snapshot.psram_largest);
    fprintf(out,
            "settings aod=%lu min tz=%ld seconds time_source=%d synced=%lld\n",
            (unsigned long)state.aod_timeout_minutes,
            (long)(state.tz_offset_seconds),
            state.time_source,
            (long long)state.last_synced_seconds);
    fprintf(out,
            "activity entries=%u prompt_reply dropped=%lu failed=%lu service=%s\n",
            snapshot.activity_count,
            (unsigned long)snapshot.prompt_reply_dropped_count,
            (unsigned long)snapshot.prompt_reply_failed_count,
            snapshot.service_error[0] ? snapshot.service_error : "<none>");
    fprintf(out,
            "buddy rx chunks=%lu bytes=%lu enqueue_failures=%lu queue_high_water=%lu max_chunk=%lu queue_wait count=%lu total=%lluus max=%luus\n",
            (unsigned long)rx_diagnostics.chunks_received,
            (unsigned long)rx_diagnostics.bytes_received,
            (unsigned long)rx_diagnostics.enqueue_failures,
            (unsigned long)rx_diagnostics.queue_high_water,
            (unsigned long)rx_diagnostics.max_chunk,
            (unsigned long)rx_diagnostics.queue_wait_count,
            (unsigned long long)rx_diagnostics.queue_wait_total_us,
            (unsigned long)rx_diagnostics.queue_wait_max_us);
    fprintf(out,
            "ble conn valid=%d interval=%u (%.2fms) latency=%u timeout=%ums updates=%lu failed=%lu\n",
            transport_diagnostics.conn_params_valid,
            (unsigned)transport_diagnostics.conn_interval_units,
            (double)transport_diagnostics.conn_interval_units * 1.25,
            (unsigned)transport_diagnostics.slave_latency,
            (unsigned)transport_diagnostics.supervision_timeout_units * 10,
            (unsigned long)transport_diagnostics.conn_param_update_count,
            (unsigned long)transport_diagnostics.conn_param_update_failures);
    fprintf(out,
            "folder chunk handler count=%lu total=%lluus max=%luus ack_encode count=%lu total=%lluus max=%luus tx_queue count=%lu total=%lluus max=%luus notify count=%lu total=%lluus max=%luus\n",
            (unsigned long)rx_diagnostics.folder_chunk_handler_count,
            (unsigned long long)rx_diagnostics.folder_chunk_handler_total_us,
            (unsigned long)rx_diagnostics.folder_chunk_handler_max_us,
            (unsigned long)rx_diagnostics.folder_chunk_ack_encode_count,
            (unsigned long long)rx_diagnostics.folder_chunk_ack_encode_total_us,
            (unsigned long)rx_diagnostics.folder_chunk_ack_encode_max_us,
            (unsigned long)transport_diagnostics.chunk_ack_tx_queue_count,
            (unsigned long long)transport_diagnostics.chunk_ack_tx_queue_total_us,
            (unsigned long)transport_diagnostics.chunk_ack_tx_queue_max_us,
            (unsigned long)transport_diagnostics.chunk_ack_notify_count,
            (unsigned long long)transport_diagnostics.chunk_ack_notify_total_us,
            (unsigned long)transport_diagnostics.chunk_ack_notify_max_us);
    fprintf(out,
            "charpack fwrite count=%lu total=%lluus max=%luus finalize count=%lu total=%lluus max=%luus decode count=%lu total=%lluus max=%luus sink_write count=%lu total=%lluus max=%luus crc count=%lu total=%lluus max=%luus\n",
            (unsigned long)charpack_diagnostics.chunk_write_count,
            (unsigned long long)charpack_diagnostics.fwrite_total_us,
            (unsigned long)charpack_diagnostics.fwrite_max_us,
            (unsigned long)charpack_diagnostics.file_finalize_count,
            (unsigned long long)charpack_diagnostics.file_finalize_total_us,
            (unsigned long)charpack_diagnostics.file_finalize_max_us,
            (unsigned long)folder_push_diagnostics.base64_decode_count,
            (unsigned long long)folder_push_diagnostics.base64_decode_total_us,
            (unsigned long)folder_push_diagnostics.base64_decode_max_us,
            (unsigned long)folder_push_diagnostics.sink_write_count,
            (unsigned long long)folder_push_diagnostics.sink_write_total_us,
            (unsigned long)folder_push_diagnostics.sink_write_max_us,
            (unsigned long)folder_push_diagnostics.crc_update_count,
            (unsigned long long)folder_push_diagnostics.crc_update_total_us,
            (unsigned long)folder_push_diagnostics.crc_update_max_us);
    if (bsp_display_get_diag(&display_diag) == ESP_OK) {
        fprintf(out,
                "display flush start=%lu finish=%lu wait=%lu/%lu color_done=%lu phase=%s\n",
                (unsigned long)display_diag.flush_start_count,
                (unsigned long)display_diag.flush_finish_count,
                (unsigned long)display_diag.flush_wait_start_count,
                (unsigned long)display_diag.flush_wait_finish_count,
                (unsigned long)display_diag.color_done_count,
                buddy_app_diag_display_phase_name(display_diag.flush_phase));
        fprintf(out,
                "touch irq=%d pen_down=%d snapshot=%u age=%lldms notify=%lu\n",
                display_diag.touch_irq_level, display_diag.touch_snapshot_pen_down,
                display_diag.touch_snapshot_count,
                display_diag.touch_snapshot_time_us > 0 ?
                    (long long)((esp_timer_get_time() - display_diag.touch_snapshot_time_us) / 1000) : -1LL,
                (unsigned long)display_diag.touch_notify_count);
        fprintf(out,
                "touch samples enter=%lu exit=%lu errors=%lu skipped=%lu last_points=%u\n",
                (unsigned long)display_diag.touch_sample_enter_count,
                (unsigned long)display_diag.touch_sample_exit_count,
                (unsigned long)display_diag.touch_sample_error_count,
                (unsigned long)display_diag.touch_sample_skip_count,
                display_diag.touch_sample_last_point_count);
        fprintf(out,
                "display spi_gate owner=%s take=%lu give=%lu\n",
                buddy_app_diag_spi_owner_name(display_diag.spi_gate_owner),
                (unsigned long)display_diag.spi_gate_take_count,
                (unsigned long)display_diag.spi_gate_give_count);
    } else {
        fprintf(out, "display diagnostics unavailable\n");
    }
    fprintf(out, "tasks\n");
    for (size_t i = 0; i < BUDDY_APP_DIAG_TASK_COUNT; ++i) {
        if (state.task_handles[i] == NULL) {
            fprintf(out, "  %-16s not-started\n", task_names[i]);
        } else {
            fprintf(out,
                    "  %-16s watermark=%lu bytes\n",
                    task_names[i],
                    (unsigned long)snapshot.task_stack_bytes[i]);
        }
    }
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    buddy_app_diag_print_runtime(out);
#endif
    fflush(out);
}
