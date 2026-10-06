# 许可证与第三方材料

本项目自有且没有另行许可声明的源码和文档采用 [MIT](LICENSE)。第三方材料适用其文件中的 SPDX 标识、版权声明、NOTICE 和原许可证；使用或再分发时应遵守相应条款。

| 材料 | 来源与许可依据 | 处理 |
|---|---|---|
| Espressif 版权源码 | 文件头 `SPDX-License-Identifier: Apache-2.0` | 保留原声明；修改仍受对应上游条款约束 |
| ESP-IDF 6.0.2 | 安装 SDK 的 `LICENSE` 与各组件声明 | 发行归档包含 SDK 许可证及已构建组件的许可文件 |
| 本地 LVGL | `components/third_party/lvgl__lvgl/LICENCE.txt`，MIT | 保留完整源码许可；嵌入的库按各自声明 |
| 本地 esp_lvgl_adapter | `components/third_party/espressif__esp_lvgl_adapter/LICENSE`，Apache-2.0 | 保留版权和许可证 |
| LVGL GIF 解码 | `components/third_party/lvgl__lvgl/src/libs/gif/LICENSE`，Apache-2.0，BitBank Software | 保留完整许可证 |
| 中文字体 Source Han Sans SC | `components/third_party/lvgl__lvgl/scripts/built_in_font/font_license/SourceHanSansSC/LICENSE.txt`，SIL OFL 1.1，Adobe | 字体子集和 binfont 随发行保留该许可；不使用保留字体名作为修改后字体产品名 |
| Montserrat | 字体许可目录的 `Montserrat/OFL.txt`，SIL OFL 1.1 | LVGL 内置数字／拉丁字体保留许可 |
| Font Awesome 字体／符号 | 字体许可目录的 `FontAwesome/LICENSE.txt` 和 `FontAwesome5/LICENSE.txt` | 字体、图标与代码的条款分别适用，完整原文随归档保留 |
| Component Manager 组件 | `dependencies.lock` 固定版本，各下载组件的 LICENSE／NOTICE | 不以组件命名推断许可；打包工具归档实际构建组件许可 |
| Vue、Vite、lv_font_conv 及 npm 间接依赖 | 各目录 lockfile 与安装包许可证 | 用于预览／字体工具，不随固件执行；源码构建遵守各包条款 |
| 运行时角色包 | 由用户／插件安装，发行 storage 当前只有字体 | 未附带角色 GIF；角色包作者应自行提供素材权利与许可说明 |

打包工具从仓库第三方目录、实际构建组件和 SDK 根目录收集名称包含 LICENSE／LICENCE／COPYING／NOTICE／OFL 的文本文件，写入调试归档的 `licenses/`，并生成 `license-index.json`。新增第三方材料时需同时确认来源和许可证，并补充本表及相应声明。
