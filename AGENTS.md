# 使用 Agent 开发

本文件供使用 AI 编程工具修改本项目的开发者参考。安装、接线和烧录见 [README](README.md)，架构、存储、UI 与协议见 [开发文档](DEVELOPMENT_GUIDE.md)。

## 项目环境

- 目标硬件：ESP32-S3 N16R8、ST7789V 240×320、XPT2046。
- 工具链：ESP-IDF 6.0.2；图形库为 LVGL 9.x。
- 构建入口为根目录 `CMakeLists.txt` 和 `sdkconfig.defaults`，依赖由 `dependencies.lock` 固定。
- 字体生成工具位于 `tools/fonts/`，可见界面的 Vue 预览位于 `tools/ui_preview/`。

## 修改入口

| 需求 | 主要位置 |
|---|---|
| 接线、显示、触摸与背光 | `components/buddy_bsp/` |
| 启动与模块连接 | `main/app_main.c` |
| 页面、布局与交互 | `main/ui/` |
| 中英文文案与字体 | `main/ui_locale.*`、`tools/fonts/` |
| 设置默认值、保存与恢复 | `main/app_settings.c` |
| Buddy 消息、审批与校时 | `components/esp_desktop_buddy/` |
| 蓝牙广播、配对与传输 | `components/esp_desktop_buddy_transport_ble/` |
| 角色包传输与安装 | `components/esp_desktop_buddy_folder_push/`、`components/example_charpack/` |

## 实现约束

- `buddy_app_t` 是应用共享状态，跨任务访问使用 `app->mutex`；字段明确注明的无锁情况除外。
- 服务通过快照和刷新标志通知 UI。LVGL 对象仅由 UI 任务在持有 adapter lock 时访问；持有 `app->mutex` 时不要调用可能立即触发 LVGL 事件的接口。
- 审批回复按顺序传递；状态快照可以保留最新一份，两者不能共用覆盖式队列。
- 设置默认值、校验和保存集中在 `main/app_settings.c`，页面不维护另一套设置。
- UI 支持 `zh_CN`、`en_US`、240×320 和 320×240，触摸区域至少为 44×44。修改可见行为时同步 `tools/ui_preview/`；修改固定中文文案后运行 `tools/fonts/generate_fonts.ps1`。
- 显示与触摸共用 SPI2，修改总线操作时保持二值信号量与 DMA 完成通知的资源生命周期。
- 存储错误应报告给调用方，不通过自动格式化清除用户角色包。文件提交和启动恢复规则见开发文档。
- 不手工修改 `managed_components/`。固定第三方源码及本地调整见 [组件说明](components/third_party/README.md)，分发时保留原版权与许可文件。
- 不将凭据、蓝牙配对码或敏感审批正文写入日志、示例或提交。
