# ESP32-S3 Buddy

一个通过蓝牙连接电脑、显示 AI  Agent 工作状态并支持触摸审批的桌面伙伴。支持通过插件连接 DeepSeek Harness，也可通过桥接工具连接 Claude Code。设备显示会话与工作状态、Token 数据和角色动画，并提供触摸审批。

当前版本为 **`0.5.0-rc.1` 候选版**。固件下载及版本兼容信息见 [GitHub Releases](https://github.com/WayAero/esp32s3-buddy/releases)。尚无发行附件时可从源码构建。

## 目录

- [项目简介](#项目简介)
- [功能与支持的 Agent](#功能与支持的-agent)
- [硬件选择与接线](#硬件选择与接线)
- [免开发环境网页烧录](#免开发环境网页烧录)
- [从零构建与串口烧录](#从零构建与串口烧录)
- [使用 VS Code 开发](#使用-vs-code-开发)
- [首次使用](#首次使用)
- [仓库结构与开发入口](#仓库结构与开发入口)
- [注意事项与故障反馈](#注意事项与故障反馈)
- [参考项目鸣谢与许可](#参考项目鸣谢与许可)
- [AI 使用声明与维护说明](#ai-使用声明与维护说明)

## 项目简介

| 页面 | 用途 |
|---|---|
| Home | 大数字时钟、Buddy 数据和最近活动摘要 |
| Buddy | 角色动画、工作状态、会话数量、Token 与估算上下文 |
| Activity | 最近事件，重启后清空，不保存敏感审批全文 |
| Settings | 显示、息屏、角色包、时间、设备名称和诊断 |

支持简体中文／英文、手动横竖屏、DeepSeek／Claude 两套品牌的浅色与深色主题，以及息屏显示（AOD）。时间由电脑通过蓝牙低功耗（BLE）同步；设备通过 BLE 工作，只需开发板、显示与触摸模块。角色包运行时安装，无须重新编译固件。当前没有 OTA 更新。

只想体验：准备硬件，按接线表连接，下载发行附件并网页烧录。希望修改固件：按从零构建流程安装工具，再阅读[开发文档](DEVELOPMENT_GUIDE.md)。

## 功能与支持的 Agent

- **状态显示**：查看连接状态、会话总数、运行与等待数量、Token 和最近活动。估算上下文在电脑端发送对应数据时显示。
- **触摸审批**：收到权限请求后，在屏幕查看操作说明，选择 `Allow Once` 或 `Deny`，将决定返回电脑端。
- **角色动画**：根据运行、等待审批、空闲和断线状态播放动画；支持安装、切换角色包并在重启后恢复选择。
- **时钟与显示**：支持电脑校时、断线后持续走时、中英文、横竖屏、四种主题、亮度设置和息屏显示。

### 接入方式

| Agent | 电脑端工具 | 接入说明 |
|---|---|---|
| DeepSeek Harness | [dsh-esp-buddy 插件](https://github.com/WayAero/dsh-esp-buddy) | 安装并启用插件，通过 BLE 连接设备；支持状态显示、审批、校时和角色包发送 |
| Claude Code | [cc-buddy-bridge](https://github.com/SnowWarri0r/cc-buddy-bridge) | 通过桥接工具传递会话状态和权限请求；设备名称需以 `Claude` 开头 |

### 连接 Claude Code

1. 在设备的 Settings → 设备中，将蓝牙名称改为 `Claude` 或 `Claude-Buddy`，保存后重启。桥接工具默认按 `Claude` 前缀查找设备，保留默认的 `DeepSeek-XXXX` 名称时无法被默认扫描发现。
2. 按 [cc-buddy-bridge 中文说明](https://github.com/SnowWarri0r/cc-buddy-bridge/blob/main/README.zh-CN.md)安装工具，执行 `cc-buddy-bridge install` 注册 Claude Code 钩子（hooks），再执行 `cc-buddy-bridge daemon` 启动桥接服务。命令应在桥接工具对应的 Python 环境中运行。
3. 启动 Claude Code 会话，按设备提示完成蓝牙配对。桥接服务连接后推送会话状态；需要设备确认的操作会显示审批界面，触摸选择后返回结果。哪些操作交给设备审批，由 Claude Code 权限配置和桥接工具的匹配规则共同决定。

设备一次只连接一个 BLE 客户端；切换接入方式前，先断开插件或退出桥接服务。更换设备名称后，电脑可能仍显示缓存名称，必要时删除旧配对再连接。Claude 主题可在皮肤设置中选择，主题本身不改变蓝牙名称。

桥接接入使用 Buddy 状态与审批协议。本固件接收 UTF-8 文本，保留桥接工具默认编码，不启用其针对其他固件的 `CC_BUDDY_CJK_TARGET` 编码选项。校时需电脑端发送本项目的校时消息，角色包发送需支持 Folder Push V2；不能直接将桥接工具面向其他固件的全部功能视为本项目支持的功能。

## 硬件选择与接线

### 准备材料

| 材料 | 选择要求 |
|---|---|
| ESP32-S3 开发板 | **N16R8：16 MB Flash、8 MB 外部伪静态内存（PSRAM）**，引出下表 GPIO，具备 USB 下载接口 |
| SPI 显示与触摸模块 | **ST7789V、240×320、XPT2046 电阻触摸**；[本项目使用的显示屏](https://item.taobao.com/item.htm?id=640013309591&skuId=4850190517773) |
| 连接与供电 | USB 数据线、短杜邦线；电脑具备可用蓝牙 |

购买时核对实际选项、分辨率、触摸控制器和引脚标注；同为 ST7789V 的模块可能采用不同接线或触摸方案。其他 Flash／PSRAM 容量、显示分辨率或触摸方案需要自行适配。

### 引脚表

**断电接线。外设按 3.3 V 供电和逻辑电平连接，所有 GND 共地；不要把 5 V 接入 ESP32-S3 GPIO。** 开发板可从其 USB 接口供电，USB 输入电压与 GPIO 电平是两回事。

| 模块信号 | ESP32-S3 | 说明 |
|---|---:|---|
| VCC / GND | 3V3 / GND | 显示与触摸共地 |
| SCK / T_CLK | GPIO12 | 显示与触摸共用 SPI2 时钟 |
| MOSI / SDA / T_DIN | GPIO11 | 共用 SPI2 数据输入 |
| LCD_SDO / MISO | 不连接 | 固件不读取 LCD 数据 |
| T_DO | GPIO8 | 触摸数据输出，SPI2 MISO |
| LCD_DC | GPIO9 | 数据／命令选择 |
| LCD_CS | GPIO10 | 显示片选 |
| LCD_RST | GPIO17 | 显示复位 |
| LCD_BL / LED | GPIO18 | PWM 背光；核对模块是否具备背光驱动电路 |
| T_CS | GPIO15 | 触摸片选 |
| T_IRQ | GPIO16 | 低有效触摸中断 |
| SD_CS | 不连接 | 当前不使用模块上的 SD 卡槽 |

LCD 的 SDO 和触摸的 T_DO 是两个不同输出，**不能接在一起**。GPIO48 状态灯为可选项。显示 SPI 当前配置为 80 MHz，触摸为 1 MHz；布线宜短，长杜邦线下的信号完整性需自行验证。接线配置位于 `components/buddy_bsp/buddy_bsp.c`。

## 免开发环境网页烧录

无需安装 ESP-IDF 或 Python；仍需 USB 数据线、可用串口及必要的 USB 转串口驱动。使用桌面 Chrome／Edge 打开 [智工具 ESP32 在线烧录](https://zutils.cn/tools/esp32-flash/)。网页通过浏览器串口接口连接开发板。

### 选择镜像

从同一条 Release 下载镜像、`SHA256SUMS.txt` 和 `release.json`，先阅读该版本的数据影响。

| 镜像 | 用途 | 当前地址 | 数据影响 |
|---|---|---|---|
| `*-full.bin` | 首次安装、完整恢复、分区不兼容时升级 | `0x0` | 重置设置、蓝牙绑定、校准数据、角色包和 storage 字体 |
| `*-app.bin` | 已有兼容分区布局时升级应用 | `0x10000` | 保留 storage 与角色包；设置按版本迁移规则处理 |

**地址、Flash 参数和兼容范围最终以下载包的 `release.json` 与发行说明为准。** 应用镜像要求设备使用相同分区布局，并具备足够的应用分区容量。不要混用不同版本的镜像或元数据。

### 操作步骤

1. 核对硬件并连接 USB 数据线，关闭占用串口的软件。用 `Get-FileHash .\镜像文件.bin -Algorithm SHA256` 核对镜像摘要。
2. 打开[烧录网页](https://zutils.cn/tools/esp32-flash/)，点击“选择串口并连接”，在浏览器弹窗中选择开发板串口。
3. 保留默认复位模式，按页面设置刷写波特率；通信不稳定时降低波特率。连接失败时，按住 BOOT，短按 RESET／EN，再松开 BOOT 后重试。
4. 在“固件文件”中点击“选择 .bin 文件”，选择一个镜像并修改地址：完整镜像为 `0x0`，应用镜像为 `0x10000`。**网页默认地址为 `0x10000`，烧录 full 镜像时必须改为 `0x0`。** 合并的 full 镜像只需一行，不必再添加 bootloader 或分区表。
5. 点击“开始刷写”，等待输出日志显示完成，再 RESET 或重新上电。full 镜像会覆盖设备数据；保留数据的升级使用兼容的 app 镜像。
6. 首页出现后，按[首次使用](#首次使用)连接电脑端工具。冷启动显示 `--:--` 表示尚未校时。

若 Release 暂无附件，可以按下一节自行构建。

## 从零构建与串口烧录

以下以 Windows 11、PowerShell 7 为例，工具安装位置按本机环境选择。

### 1. 安装工具并激活环境

- 安装 Git。
- 按 [Espressif 官方安装说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/windows-setup.html)安装 ESP-IDF 和 ESP32-S3 工具链，**选择 ESP-IDF 6.0.2**；官方 stable 文档可能已介绍更新版本。
- 打开安装器提供的 ESP-IDF 终端／PowerShell 快捷方式，确认 `idf.py --version` 输出 6.0.2。普通 PowerShell 未必已激活工具链。
- 安装 Node.js 22 LTS 或更新的兼容版本及 npm 10+，用于字体和 Vue 预览。字体转换器由 lockfile 固定，不需要全局安装。

### 2. 获取源码和工具依赖

构建某个发行版本时，请检出该 Release 标注的标签或提交。

```powershell
git clone https://github.com/WayAero/esp32s3-buddy.git
Set-Location esp32s3_buddy
idf.py --version
node --version
npm --version
npm ci --prefix tools/fonts
npm ci --prefix tools/ui_preview
```

首次构建需要联网获取锁定组件。`dependencies.lock`、`sdkconfig.defaults` 和固定第三方源码已随仓库提供；不需要预先复制 `sdkconfig`、`managed_components/`、`build/` 或 `node_modules/`。

### 3. 构建

```powershell
.\tools\fonts\generate_fonts.ps1
idf.py set-target esp32s3
idf.py build
python tools/normalize_dependency_locks.py
```

`set-target` 用于首次配置目标，不需要每次构建都执行。生成字体会同步应用与 storage 字体；仅修改固件而不改文案时可以复用仓库内字体。Windows Component Manager 可能写入反斜杠路径，最后一条命令将依赖锁路径恢复为跨平台表示，不改变版本或哈希。

### 4. 烧录与查看日志

将 `COM5` 替换为开发板实际串口。**下面的完整构建烧录会写入 storage 初始镜像，覆盖已有角色包；已有设备日常升级使用应用烧录。**

```powershell
idf.py -p COM5 flash monitor
```

兼容分区下只更新应用：

```powershell
idf.py -p COM5 app-flash monitor
```

用 `Ctrl+]` 退出监视器。应用烧录保留 storage。构建产物位于 `build/`。

预览、打包和构建故障处理见[开发文档](DEVELOPMENT_GUIDE.md)。

## 使用 VS Code 开发

VS Code 可以在同一个窗口编辑、编译、烧录和查看日志，底层仍使用 ESP-IDF 工具链。完成上节的源码获取和工具依赖安装后，按以下步骤配置。

### 1. 安装编辑器与扩展

从 [VS Code 官网](https://code.visualstudio.com/)安装编辑器，在扩展市场安装 Espressif 发布的 **ESP-IDF**（`espressif.esp-idf-extension`）。简体中文语言包和 Microsoft C/C++ 扩展可按需安装。

### 2. 打开项目并选择 SDK

1. 使用“文件 → 打开文件夹”打开仓库根目录，也可在该目录运行 `code .`。
2. 按 `Ctrl+Shift+P` 打开命令面板，执行 `ESP-IDF: Select Current ESP-IDF Version`，选择已安装的 **6.0.2**。
3. 如尚未安装 SDK，执行 `ESP-IDF: Open ESP-IDF Installation Manager`，安装 6.0.2 及 ESP32-S3 工具链，再选择对应环境。
4. 执行 `ESP-IDF: Doctor Command` 检查配置。非 EIM 安装的环境可按[官方扩展安装说明](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html)配置已有工具链。

项目根目录应能看到 `CMakeLists.txt` 和 `sdkconfig.defaults`。SDK 路径、串口与编辑器配置随本机设置，`.vscode/` 不纳入 Git。

### 3. 编译与烧录

| 操作 | VS Code 命令或入口 |
|---|---|
| 首次选择目标 | `ESP-IDF: Set Espressif Device Target` → `esp32s3` |
| 编译 | `ESP-IDF: Build your Project` |
| 选择串口 | `ESP-IDF: Select Port to Use`，选择开发板实际 COM 端口 |
| 完整烧录 | `ESP-IDF: Flash your Project`，使用串口／UART 方式 |
| 查看运行日志 | `ESP-IDF: Monitor your device` |

完整烧录会写入项目配置的 storage 初始镜像，覆盖已有角色包。保留 storage 的应用升级，请在扩展的 ESP-IDF 终端使用 `idf.py -p COM5 app-flash monitor`，替换实际串口。

扩展命令及菜单可能随版本变化，完整操作见[官方编译说明](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/buildproject.html)和[烧录说明](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/flashdevice.html)。构建失败时检查输出日志中的首个错误、SDK 版本和环境诊断；同一个 `build/` 目录一次只运行一个构建任务。CLI 与扩展使用的 SDK 应保持一致。

## 首次使用

1. 在电脑蓝牙设置中找到设备，默认名称为 `DeepSeek-XXXX`，按屏幕上的六位配对码完成配对。
2. 选择[接入方式](#接入方式)：DeepSeek Harness 安装插件；Claude Code 先修改设备名称，再安装桥接工具。
3. 电脑端发送有效校时消息后，Home 显示时间。持续通电时断线仍继续走时；重新上电须再次校时。未收到有效校时消息时显示 `--:--`。
4. 使用支持 Folder Push V2 的电脑端工具发送角色包，在 Settings → 角色包切换已安装包。当前上限为单文件 1 MiB、单包 4 MiB，还受设备剩余空间约束。
5. 无角色包时仍能查看状态与处理审批。AOD 下首次触摸只唤醒，再次触摸才操作页面。

## 仓库结构与开发入口

```text
main/                     应用启动、共享状态、设置、UI、串口维护
components/buddy_bsp/     显示、触摸、背光和总线控制
components/esp_desktop_buddy*/  Buddy 协议、BLE 和角色包传输
components/third_party/   固定的 LVGL 与 adapter 源码和上游声明
storage/                  初始 storage 镜像输入，含动态字体
tools/fonts/              固定版本的字体生成工具
tools/ui_preview/         当前 UI 的 Vue 预览
tools/release/            本地发行打包工具
```

| 文档 | 阅读对象与内容 |
|---|---|
| [README](README.md) | 新用户：硬件、安装、使用与排查入口 |
| [开发文档](DEVELOPMENT_GUIDE.md) | 开发者：架构、锁、存储、UI、协议与构建 |
| [Agent 开发说明](AGENTS.md) | 使用 AI 编程工具：项目入口、模块职责与实现约束 |
| [第三方声明](THIRD_PARTY_NOTICES.md) | 源码、字体与发行附件的许可范围 |

各版本变更、兼容性和下载附件集中在 [GitHub Releases](https://github.com/WayAero/esp32s3-buddy/releases)。

## 注意事项与故障反馈

- **黑屏或重启**：先查供电、共地、CS／DC／RST／BL 和串口启动日志，不通过反复擦除掩盖错误。
- **串口无法连接**：检查数据线、驱动、下载模式和串口占用；一块板可能有不同用途的 USB 接口，按板卡说明选择。
- **蓝牙状态或时间异常**：区分发现、配对、加密、插件连接与设备实际应用校时；必要时删除双方旧绑定后重新配对。
- **存储异常**：固件不会自动格式化。`storage format ERASE` 是挂载失败时的明确维护操作，会删除角色包与 storage 字体；普通故障排查不应直接执行。
- **数据与安全**：Flash／NVS 未启用加密，不能防物理读取。逻辑删键不是安全擦除；转让设备前应按自己的数据要求处理，完整擦除会丢失全部设备数据。

串口 `status`／`diag` 提供状态和诊断。反馈请附固件版本／提交、插件版本、硬件、镜像类型、复现步骤和相关错误日志；不要附配对码、凭据或敏感审批正文。更详细的维护命令见[开发文档](DEVELOPMENT_GUIDE.md#调试与维护)。

## 参考项目鸣谢与许可

感谢以下开源项目及其作者提供的代码、工具和技术资料：

- [Espressif ESP Desktop Buddy](https://github.com/espressif/esp-desktop-buddy)：Buddy 协议核心、BLE 传输与文件推送的上游基础。
- [Anthropic Claude Desktop Buddy](https://github.com/anthropics/claude-desktop-buddy)：上游协议与交互参考。
- [ESP-IDF](https://github.com/espressif/esp-idf)、[LVGL](https://github.com/lvgl/lvgl) 与 [esp-iot-solution](https://github.com/espressif/esp-iot-solution)：嵌入式框架、图形库与 adapter 上游。
- [智工具 ESP32 在线烧录](https://zutils.cn/tools/esp32-flash/)：浏览器烧录入口。
- [cc-buddy-bridge](https://github.com/SnowWarri0r/cc-buddy-bridge)：Claude Code 会话与审批桥接工具。

也感谢字体与触摸驱动等第三方组件的作者，以及参与复刻、问题反馈和修正的贡献者。具体依赖与版权声明见下方许可清单。

自有且未另行标注的代码采用 [MIT](LICENSE)。上游 Apache、LVGL、字体和其他材料保持各自许可，根许可证不替代原有声明；具体范围见[第三方声明](THIRD_PARTY_NOTICES.md)。

## AI 使用声明与维护说明

本项目使用 AI 辅助编写代码、排查问题和整理文档。AI 生成的内容可能存在错误，使用和修改时请结合源码、工具输出及设备实际表现判断。

欢迎复刻、学习、提交问题和自行维护分支。本人精力有限，后续可能无法持续维护，也无法保证问题响应和版本更新时限。建议复刻时保存使用版本、接线和配套插件信息，按自身场景验证可靠性，并遵守源码、字体与角色素材的原有许可。
