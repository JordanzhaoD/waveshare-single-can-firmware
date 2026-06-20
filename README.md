# LILYGO T-2CAN — Tesla Model Y FSD CAN Dashboard

**中文** | [English](#english)

---

## 中文

### 项目简介

本项目是基于 [EV Open CAN Tools](https://github.com/ev-open-can-tools/ev-open-can-tools) 的固件，专为 **LILYGO T-2CAN**（ESP32-S3 N16R8）开发板适配，连接特斯拉 Model Y **X197 连接器**，实现双路 CAN 总线同时工作：

- **CAN 总线 1（TWAI，X197 Pin 13/14）**：FSD 功能解锁、车速控制
- **CAN 总线 2（MCP2515 SPI，X197 Pin 9/10）**：维修模式控制、灯光控制（开发中）

通过板载 WiFi 热点提供 Web 控制面板，无需手机 App，浏览器直接访问。

---

### 硬件规格

| 参数 | 配置 |
|------|------|
| 开发板 | LILYGO T-2CAN |
| 主控 | ESP32-S3 N16R8 |
| CPU | 240 MHz 双核 |
| Flash | 16 MB QIO 80 MHz |
| PSRAM | 8 MB OPI 80 MHz |
| CAN 1 | ESP32-S3 TWAI（GPIO 7 TX / GPIO 6 RX） |
| CAN 2 | MCP2515 SPI（SCK=12 MOSI=11 MISO=13 CS=10 RST=9） |
| CAN 2 晶振 | 16 MHz |

---

### X197 接线说明

| X197 引脚 | 功能 | 连接 |
|-----------|------|------|
| Pin 13 | CAN High（总线 1） | TWAI TX → GPIO 7 |
| Pin 14 | CAN Low（总线 1） | TWAI RX → GPIO 6 |
| Pin 9 | CAN High（总线 2） | MCP2515 CAN H |
| Pin 10 | CAN Low（总线 2） | MCP2515 CAN L |

> **注意：** CAN 总线需要 120Ω 终端电阻。请确认车辆端已有终端电阻，否则需外接。

### Waveshare 单 CAN 独立版

微雪 ESP32-S3-RS485-CAN 请使用 `waveshare_single_can_standalone` 环境。该版本只使用单路 TWAI CAN（GPIO15 TX / GPIO16 RX），保留 FSD 激活、速度策略、驾驶风格、网络设置、OTA 升级和单 CAN 诊断；CAN2、MCP2515、Bus2、Service Mode、Stalk/highbeam、灯光特技入口会按 capabilities 隐藏或不注册。

Dashboard 同时暴露 AP Gate、延迟注入、AP 注入状态、官方 JSON 插件管理（URL 安装 / `.json` 上传 / 离线粘贴、默认关闭、优先级、移除、详情）以及 GTW2047 重放倍率；插件状态持久化到 SPIFFS，重启后自动恢复。

```bash
pio run -e waveshare_single_can_standalone
```

AP Injection Gate 安全策略对齐 `ev-open-can-tools v3.0.2-beta.3`：Legacy `0x3EE mux0` 在 gate 开启时必须等待 AP active 连续 2 秒；unknown/invalid/SNA gear 默认 fail-closed。该策略降低 2026.8.3.6 相关误注入风险，但不承诺全车型直接兼容。

---

### 功能列表

#### 已完成

- [x] 双路 CAN 同时运行（TWAI + MCP2515）
- [x] FSD 功能解锁（CAN 总线 1）
- [x] 车速控制（CAN 总线 1）
- [x] Web 控制面板（WiFi AP 模式）
- [x] 维修模式开关（按 VCSEC 规格发送 0x339 四帧脉冲）
- [x] Bus2 嗅探器（实时显示 X197 9/10 上的 CAN 帧）
- [x] USB-Serial/JTAG 控制台输出
- [x] OTA 固件在线升级
- [x] 系统状态显示（含开发板名称 LILYGO-T-2CAN）

#### 逆向研究结论

- [x] 0x249 SCCMLeftStalk CRC 已验证，可做短时 PULL/PUSH 测试注入。
- [x] 远光/自适应灯光共享总线注入边界已确认：`0x3F5 byte1 bit7` 为手动/自动模式，`0x3F5 byte3` 为远光开关与驱动来源，`0x293 byte2 bit6` 为自适应大灯使能。
- [ ] FSD 激活状态下强制覆盖远光暂不实现：车灯 ECU 按内部状态决策，FSD 会强制自适应逻辑；共享总线注入压不过内部逻辑，若要实现需要 inline MITM（剪断 X197 9/10、双 CAN 串接改写）。
- [ ] Bus2 帧录制与回放

---

### 快速开始

#### 1. 环境要求

- [PlatformIO](https://platformio.org/) + VS Code
- Python 3.x（用于 UI 构建脚本）
- ESP-IDF v6.0.1（由 PlatformIO 自动管理）

#### 2. 配置密钥文件

```bash
cp platformio_profile.example.h platformio_profile.h
```

编辑 `platformio_profile.h`，填入：

```cpp
#define DASH_SSID      "LILYGO-T-2CAN"   // WiFi 热点名称
#define DASH_PASS      "your_password"    // WiFi 密码
#define DASH_OTA_USER  "admin"            // OTA 用户名
#define DASH_OTA_PASS  "your_ota_pass"    // OTA 密码
```

> ⚠️ `platformio_profile.h` 已加入 `.gitignore`，**不会提交到 Git**，请妥善保管。

#### 3. 编译与烧录

```bash
# 编译
pio run -e lilygo_t2can_dual

# 烧录（替换为实际 COM 端口）
pio run -e lilygo_t2can_dual -t upload --upload-port COM20
```

#### 4. 访问面板

1. 手机或电脑连接 WiFi：`LILYGO-T-2CAN`
2. 浏览器打开：`http://100.100.1.1/`

---

### 维修模式

在 Web 面板中找到 **Service Mode** 开关。按官方规格（VCSEC_serviceDiagnosticRequest，start bit 47 = byte5 bit7），每次切换发送 **4 帧、间隔 10ms** 的脉冲到 X197 Pin 9/10：

```
开启: ID 0x339  DLC 8  Data 00 00 00 00 00 80 00 00   (byte5 bit7 = 1)
关闭: ID 0x339  DLC 8  Data 00 00 00 00 00 00 00 00
```

该开关是 RAM-only，重启后默认关闭。

---

### 安全警告

> ⚠️ **重要：** 对车辆 CAN 总线进行任何修改都存在风险。CAN 总线涉及转向、制动、安全气囊等安全关键系统。请仅在充分了解相关帧含义的情况下使用本项目。本项目仅供学习和测试用途，使用者自行承担一切风险和法律责任。

---

### 许可证

本项目基于 **GNU General Public License v3.0** 授权。

---

---

<a name="english"></a>

## English

### Overview

This project is a firmware adaptation of [EV Open CAN Tools](https://github.com/ev-open-can-tools/ev-open-can-tools) for the **LILYGO T-2CAN** (ESP32-S3 N16R8) development board. It connects to a Tesla Model Y via the **X197 connector** and operates dual CAN buses simultaneously:

- **CAN Bus 1 (TWAI, X197 Pin 13/14)**: FSD unlock, speed control
- **CAN Bus 2 (MCP2515 SPI, X197 Pin 9/10)**: Service mode control, lighting control (in development)

A web dashboard is served over the onboard WiFi hotspot — no app needed, just a browser.

---

### Hardware Specifications

| Parameter | Value |
|-----------|-------|
| Board | LILYGO T-2CAN |
| MCU | ESP32-S3 N16R8 |
| CPU | 240 MHz dual-core |
| Flash | 16 MB QIO 80 MHz |
| PSRAM | 8 MB OPI 80 MHz |
| CAN 1 | ESP32-S3 TWAI (GPIO 7 TX / GPIO 6 RX) |
| CAN 2 | MCP2515 SPI (SCK=12 MOSI=11 MISO=13 CS=10 RST=9) |
| CAN 2 Crystal | 16 MHz |

---

### X197 Wiring

| X197 Pin | Function | Connection |
|----------|----------|------------|
| Pin 13 | CAN High (Bus 1) | TWAI TX → GPIO 7 |
| Pin 14 | CAN Low (Bus 1) | TWAI RX → GPIO 6 |
| Pin 9 | CAN High (Bus 2) | MCP2515 CAN H |
| Pin 10 | CAN Low (Bus 2) | MCP2515 CAN L |

> **Note:** CAN buses require 120Ω termination resistors. Verify that the vehicle side already has termination; add external resistors if not.

---

### Features

#### Completed

- [x] Dual CAN simultaneous operation (TWAI + MCP2515)
- [x] FSD unlock (CAN Bus 1)
- [x] Speed control (CAN Bus 1)
- [x] Web dashboard (WiFi AP mode)
- [x] Service mode toggle (VCSEC-spec 4-frame 0x339 pulse)
- [x] Bus2 sniffer (live view of CAN frames on X197 Pin 9/10)
- [x] USB-Serial/JTAG console output
- [x] OTA firmware updates
- [x] System status with board name (LILYGO-T-2CAN)

#### Reverse-Engineering Findings

- [x] 0x249 SCCMLeftStalk CRC is verified for short PULL/PUSH test injection.
- [x] Shared-bus high-beam/adaptive-lighting limits are documented: `0x3F5 byte1 bit7` is manual/auto mode, `0x3F5 byte3` carries high-beam state/source, and `0x293 byte2 bit6` is adaptive-high-beam enable.
- [ ] Forced high-beam override during active FSD is not implemented: the lighting ECU decides from internal state and FSD forces adaptive logic, so shared-bus injection cannot override it. Achieving this would require inline MITM wiring (cut X197 9/10 and rewrite traffic through two CAN interfaces).
- [ ] Bus2 frame recording and playback

---

### Quick Start

#### 1. Prerequisites

- [PlatformIO](https://platformio.org/) + VS Code
- Python 3.x (for UI build scripts)
- ESP-IDF v6.0.1 (managed automatically by PlatformIO)

#### 2. Configure Secrets

```bash
cp platformio_profile.example.h platformio_profile.h
```

Edit `platformio_profile.h`:

```cpp
#define DASH_SSID      "LILYGO-T-2CAN"   // WiFi hotspot SSID
#define DASH_PASS      "your_password"    // WiFi password
#define DASH_OTA_USER  "admin"            // OTA username
#define DASH_OTA_PASS  "your_ota_pass"    // OTA password
```

> ⚠️ `platformio_profile.h` is listed in `.gitignore` and will **never be committed**. Keep it safe.

#### 3. Build & Flash

```bash
# Build
pio run -e lilygo_t2can_dual

# Flash (replace with your actual COM port)
pio run -e lilygo_t2can_dual -t upload --upload-port COM20
```

#### 4. Access the Dashboard

1. Connect your phone or PC to WiFi: `LILYGO-T-2CAN`
2. Open browser: `http://100.100.1.1/`

---

### Service Mode

Toggle **Service Mode** in the web dashboard. Per the official VCSEC_serviceDiagnosticRequest spec (start bit 47 = byte5 bit7), each toggle fires a **4-frame burst at 10ms spacing** on X197 Pin 9/10:

```
Enable:  ID 0x339  DLC 8  Data 00 00 00 00 00 80 00 00   (byte5 bit7 = 1)
Disable: ID 0x339  DLC 8  Data 00 00 00 00 00 00 00 00
```

This switch is RAM-only and defaults to off after reboot.

---

### Safety Warning

> ⚠️ **Important:** Modifying CAN bus traffic carries real risk. The CAN bus touches safety-critical systems including steering, braking, and airbags. Only use this firmware if you fully understand the frames you are working with. This project is for educational and testing purposes only. You are solely responsible for any consequences.

---

### License

Licensed under the **GNU General Public License v3.0**.

---

### Upstream Project

Based on [EV Open CAN Tools](https://github.com/ev-open-can-tools/ev-open-can-tools) — an open-source CAN bus modification framework for electric vehicles.
