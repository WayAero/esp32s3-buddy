# 固定第三方组件

本目录保存 LVGL 和 esp_lvgl_adapter 的固定源码。ESP-IDF Component Manager 通过 `override_path` 编译这里的组件，版本来源与项目修改如下。

| 组件 | 上游版本与提交 | 本地改动 |
| --- | --- | --- |
| `lvgl__lvgl` | LVGL `9.5.0`，`85aa60d18b3d5e5588d7b247abf90198f07c8a63` | `src/osal/lv_freertos.c` 将软件绘制线程固定到 `CONFIG_BUDDY_LVGL_CORE`；线程栈按照 `LV_FREERTOS_TASK_STACK_ALLOC_CAPS` 配置分配。 |
| `espressif__esp_lvgl_adapter` | `esp_lvgl_adapter` `0.6.2`，`24e1d162eed2c461d9938274444938dc1ab88ee1` 的 `components/display/tools/esp_lvgl_adapter` 子目录 | `esp_lv_adapter.c` 记录递归 LVGL 锁持有者、深度、持有时间和 `lv_timer_handler()` 时间。 |

更新组件时记录新的上游版本与提交，将表中的项目修改应用到新版本，再重新构建固件。项目补丁直接维护在固定源码中，构建时不改写依赖源文件。上游许可证、迁移指南和变更记录随源码保存。
