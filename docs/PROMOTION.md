# Atlas-FSD 推广文案

> 本文件为 **Atlas-FSD**（Waveshare 单 CAN 版 Tesla FSD CAN Dashboard）的推广文案包，按发布平台分层，可直接取用。
>
> ⚠️ **所有对外发布务必保留免责声明**（研究/教育用途，禁公共道路，风险自负）。

---

## 推广介绍链接

| 用途 | 链接 |
|------|------|
| 主链接（仓库首页） | <https://github.com/JordanzhaoD/waveshare-single-can-firmware> |
| README（中文介绍） | <https://github.com/JordanzhaoD/waveshare-single-can-firmware#中文> |
| 界面预览（截图墙） | <https://github.com/JordanzhaoD/waveshare-single-can-firmware#-界面预览> |
| 最新固件下载 | <https://github.com/JordanzhaoD/waveshare-single-can-firmware/releases/latest> |
| v1.0.1 Release | <https://github.com/JordanzhaoD/waveshare-single-can-firmware/releases/tag/v1.0.1-atlas-single-can> |

> 发社交平台建议用**仓库首页链接**或 **Release 链接**（转化最高）。

---

## 一句话定位（elevator pitch）

> **开源的 Tesla FSD CAN 总线研究仪表盘 —— 一块百元级的 ESP32-S3 开发板 + 一个浏览器，就能把车机 FSD 数据"开"到手机/桌面上，研究、调试、可视化。**

---

## 极简版（X / 朋友圈 / 微博，<150 字）

> 开源了一个 Tesla FSD CAN 总线研究工具 🔧：Waveshare ESP32-S3 单 CAN 板 + 浏览器 Web Dashboard，FSD 注入状态可视化、速度策略/硬件模式运行时切换、CAN 诊断、插件引擎、OTA 在线升级。GPL-3.0，研究教育用途。
> 🔗 <https://github.com/JordanzhaoD/waveshare-single-can-firmware>
> #Tesla #FSD #ESP32 #CAN #开源

---

## 标准版（V2EX / Reddit / 即刻 / 论坛帖）

### 标题候选

- 开源：用一块 ESP32-S3 把 Tesla FSD 的 CAN 总线"开"到浏览器里
- 给 Tesla FSD 做了个开源的 CAN 研究仪表盘（ESP32 + Web UI，GPL-3.0）
- Atlas-FSD：开源 Tesla FSD CAN Dashboard，手机/车机浏览器直连

### 正文

大家好，开源了一个小项目：**Atlas-FSD**（Waveshare 单 CAN 版 Tesla FSD CAN Dashboard）。

**它是什么** —— 一块百元级的 Waveshare ESP32-S3-RS485-CAN 开发板，接入车辆的 party CAN 总线，板载 WiFi 热点，手机/电脑/车机浏览器直接打开 `http://100.100.1.1/` 就是一个驾驶舱风格的实时控制面板。无需 App。

**能做什么：**

- **FSD 注入状态可视化**：Legacy / HW3 / HW4 三种协议模式，运行时切换
- **速度策略 / 驾驶风格**：速度偏移、视觉限速识别开关、Auto/Sloth 模式
- **安全门控**：AP Injection Gate（未确认 AP 激活时 fail-closed），方向盘检测屏蔽（bit-19 安全路径）
- **插件引擎**：JSON 插件（URL 安装 / `.json` 上传 / 离线粘贴），优先级管理，SPIFFS 持久化
- **CAN 诊断**：CAN1/CAN2 实时帧、错误帧、时间线
- **OTA 在线升级**：检查 GitHub Release 一键更新

**技术栈：** ESP-IDF + PlatformIO + ArduinoJson，16MB Flash，单 TWAI CAN。Dashboard 是 gzip 压缩内嵌 HTML，原生响应式（桌面横屏 / 手机竖屏自适应）。

**上手 3 步：** 下载 Release 固件 → `./flash.sh /dev/你的串口` → 连 `Atlas-FSD` 热点打开浏览器。

**链接：** <https://github.com/JordanzhaoD/waveshare-single-can-firmware>

⚠️ **免责声明**：仅供研究、教育与工程学习目的，严禁在公共道路使用，可能违反当地法规与车辆服务条款，风险自负。

---

## 完整版大纲（知乎 / 博客 / Medium）

**标题建议：** 《我开源了一个 Tesla FSD CAN 总线研究仪表盘：ESP32 + 浏览器，把自动驾驶数据"开"出来》

**结构：**

1. **为什么做这个** —— CAN 总线是车辆的"神经系统"，FSD 的行为都流经它。想把 FSD 的实际数据/状态可视化、做研究调试，市面工具要么贵要么闭源。
2. **设计目标** —— 低成本（百元级 ESP32）、零 App（浏览器直连）、开源（GPL-3.0）、安全（多重门控）。
3. **硬件方案** —— Waveshare ESP32-S3-RS485-CAN，TWAI 单 CAN，16MB Flash 分区（OTA + SPIFFS + coredump）。
4. **软件架构** —— ESP-IDF 构建、Dashboard 单文件 gzip 内嵌（省 Flash）、响应式 Web UI、JSON 插件引擎、OTA 通道。
5. **核心功能演示（配 8 张截图）** —— 驾驶状态中心 / 驾驶风格 / 速度策略 / 硬件模式 / CAN 诊断 / FSD 防护 / DNS·过滤 / 手机现场遥控。
6. **安全设计** —— AP Injection Gate fail-closed、bit-19 baseline、三重安全门控、免责边界。
7. **上手教程** —— 烧录、连热点、自定义凭据、OTA。
8. **路线图** —— 欢迎社区贡献（issue/PR）。
9. **免责与合规**。

---

## 英文版（Reddit / Hackaday / X 国际）

> **Open-sourced a Tesla FSD CAN-bus research dashboard — an ESP32-S3 + a browser.**
>
> Atlas-FSD: a GPL-3.0 firmware for the Waveshare ESP32-S3-RS485-CAN board. It taps the vehicle's party CAN bus and serves a cockpit-style web dashboard over WiFi — no app, just open `http://100.100.1.1/` from your phone/laptop.
>
> Features: FSD injection status (Legacy/HW3/HW4 runtime switching), speed policy, vision speed-limit override, AP Injection Gate (fail-closed safety), plugin engine (JSON install), live CAN diagnostics, OTA updates from GitHub Releases. Responsive UI (desktop + mobile).
>
> 🔗 <https://github.com/JordanzhaoD/waveshare-single-can-firmware>
>
> ⚠️ Research / educational use only. Not for public roads. Use at your own risk.
>
> #Tesla #FSD #ESP32 #CANbus #OpenSource #IoT

---

## 关键词 & Hashtag

`#Tesla` `#TeslaFSD` `#FSD` `#CAN总线` `#CANbus` `#ESP32` `#ESP32S3` `#Waveshare` `#开源` `#OpenSource` `#GPL` `#IoT` `#自动驾驶` `#自动驾驶研究` `#车机` `#Dashboard` `#OTA` `#逆向工程` `#嵌入开发`

---

## 推广渠道建议（按优先级）

| 渠道 | 适配版本 | 备注 |
|------|---------|------|
| **V2EX**（/t 创造 / GO 节点） | 标准版 | 技术社区，转化高 |
| **即刻**（圈子：极客/硬件） | 极简/标准版 | 互动好 |
| **知乎**（专栏文章） | 完整版 | 长尾流量 |
| **B 站**（演示视频 + 简介） | 视频版 | 视觉冲击，演示 Dashboard |
| **X / Twitter** | 极简版（中英） | 国际曝光 |
| **Reddit**（r/esp32, r/teslamotors） | 英文版 | 国际社区，注意版规 |
| **GitHub Trending** | — | 鼓励用户 ⭐，冲 trending |
| **小红书 / 抖音** | 演示短视频 | 大众曝光，强调"可视化"卖点 |

---

## 合规提示（重要）

1. **所有文案务必保留免责声明**（研究/教育用途，禁公共道路，风险自负）—— 这是项目专业性和个人保护的底线。
2. **避免用"破解 / 绕过 / hack"等词**，用"研究 / 可视化 / 调试 / 逆向工程学习"。
3. **不展示具体违规操作**（如实际道路使用画面），展示台架/静态。
4. 视频/截图建议**车机界面为主**，避免车辆识别信息。
