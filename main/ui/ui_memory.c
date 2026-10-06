/* SPDX-License-Identifier: Apache-2.0 */

#include "lvgl.h"
#include "esp_heap_caps.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#define UI_MEMORY_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

/* 界面对象不使用内部 DMA 内存；显示驱动单独分配其绘图缓冲。 */
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }
void *lv_malloc_core(size_t size) { return heap_caps_malloc(size, UI_MEMORY_CAPS); }
void *lv_realloc_core(void *ptr, size_t size) { return heap_caps_realloc(ptr, size, UI_MEMORY_CAPS); }
void lv_free_core(void *ptr) { heap_caps_free(ptr); }

void lv_mem_monitor_core(lv_mem_monitor_t *monitor)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, UI_MEMORY_CAPS);
    /* 这里统计共享 PSRAM 堆，包含其他服务的分配。 */
    *monitor = (lv_mem_monitor_t) {
        .total_size = info.total_free_bytes + info.total_allocated_bytes,
        .free_cnt = info.free_blocks,
        .free_size = info.total_free_bytes,
        .free_biggest_size = info.largest_free_block,
        .used_cnt = info.allocated_blocks,
    };
    monitor->max_used = monitor->total_size - info.minimum_free_bytes;
    if (monitor->total_size != 0)
        monitor->used_pct = (uint8_t)(100ULL * info.total_allocated_bytes / monitor->total_size);
    if (monitor->free_size != 0)
        monitor->frag_pct = (uint8_t)(100ULL - 100ULL * monitor->free_biggest_size / monitor->free_size);
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(UI_MEMORY_CAPS, true) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif
