# ESP32-S3 配置保存位置总结

生成时间：2026-05-15
项目路径：`C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT`
构建环境：`waveshare_ESP32_S3_RS485_CAN`
分区表：`partitions_16mb_ota_4096k_nvs64.csv`

## 1. 核心结论

当前项目的用户配置不是保存在 ROM，也不是主要保存在 RAM，而是保存在 ESP32-S3 外部 Flash 的不同分区里。

普通下载：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload
```

只会更新固件相关区域，通常不会清除 NVS 和 SPIFFS 里的用户配置。

清除后下载：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t erase
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload
```

会清掉整片 Flash，包括 NVS、SPIFFS、OTA 信息、固件和所有用户配置。

## 2. 当前 16MB 分区布局

来源文件：`partitions_16mb_ota_4096k_nvs64.csv`

| 分区 | 类型 | 起始地址 | 大小 | 作用 |
|---|---:|---:|---:|---|
| bootloader | boot | `0x00000000` | 平台默认 | 启动加载器，不在 CSV 中显式列出 |
| partition table | data | `0x00008000` | 平台默认 | 分区表 |
| nvs | data/nvs | `0x00009000` | `0x10000` | NVS 配置保存区 |
| otadata | data/ota | `0x00019000` | `0x2000` | 当前 OTA 启动槽信息 |
| app0 | app/ota_0 | `0x00020000` | `0x400000` | 固件槽 0 |
| app1 | app/ota_1 | `0x00420000` | `0x400000` | 固件槽 1 |
| spiffs | data/spiffs | `0x00820000` | `0x7C0000` | SPIFFS 文件系统 |
| coredump | data/coredump | `0x00FE0000` | `0x20000` | 崩溃转储 |

## 3. ROM、Flash、RAM 的区别

### ROM

ROM 是 ESP32-S3 芯片内部固化的启动代码，项目运行配置不会写入 ROM。

结论：

```text
项目配置不保存在 ROM。
```

### Flash

Flash 是板子上的非易失存储。断电后仍保留。

当前项目主要把配置保存在：

```text
Flash / NVS 分区
Flash / SPIFFS 分区
```

### RAM / PSRAM

RAM 和 PSRAM 是运行时内存。重启或断电后丢失。

例如 RX/TX 计数、实时 CAN 状态、DNS cache、临时日志等都属于 RAM 状态。

## 4. NVS 分区保存的配置

NVS 分区地址：

```text
0x00009000 - 0x00018FFF
```

大小：

```text
0x10000 = 64KB
```

普通 `pio upload` 不会清除 NVS。

当前项目主要使用三个 NVS namespace：

```text
ADunlock
can
gw
```

## 5. NVS namespace：ADunlock

代码位置：

```text
include\web\mcp2515_dashboard.h
#define PREFS_NS "ADunlock"
```

保存主要 Dashboard、FSD、WiFi、限速设置。

### FSD / CAN 逻辑相关

| Key | 作用 |
|---|---|
| `hw` | 当前硬件模式：Legacy / HW3 / HW4 |
| `hw_def` | 编译默认硬件模式记录，用于默认模式迁移 |
| `can` | FSD 总开关 |
| `force_act` | FSD force activate 状态 |
| `ap_gate` | AP Gate 开关 |
| `sp_auto` | 驾驶模式是否 Auto |
| `sp_sel` | 手动驾驶模式档位 |
| `eprn` | Dashboard 调试日志开关 |

### HW3 自定义限速

| Key | 作用 |
|---|---|
| `h3_slw` | HW3 缓降开关 |
| `h3_srt` | HW3 缓降速率 |
| `h3_cust` | HW3 自定义限速开关 |
| `h3_hse` | HW3 高速分段开关 |
| `h3_enc` | HW3 wire encoding |
| `h3_ct0` - `h3_ct4` | HW3 30/40/50/60/70 分段目标速度 |
| `h3_ht0` - `h3_ht2` | HW3 80/100/120 高速分段目标速度 |

### Legacy 自定义限速

| Key | 作用 |
|---|---|
| `lg_mpp_en` | Legacy MPP 限速总开关 |
| `lg_mppc_en` | Legacy 自定义分段开关 |
| `lg_mpph_en` | Legacy 高速分段开关 |
| `lg_ct0` - `lg_ct4` | Legacy 30/40/50/60/70 分段目标速度 |
| `lg_ht0` - `lg_ht2` | Legacy 80/100/120 高速分段目标速度 |

### AP 热点设置

| Key | 作用 |
|---|---|
| `ap_ssid` | ESP32 AP 热点名称 |
| `ap_pass` | ESP32 AP 热点密码 |
| `ap_hidden` | AP 是否隐藏 |

### STA WiFi 设置

| Key | 作用 |
|---|---|
| `wn_cnt` | 保存的 WiFi 数量 |
| `wn_pref` | 优先 WiFi 槽位 |
| `w0s`, `w1s`... | WiFi SSID |
| `w0p`, `w1p`... | WiFi 密码 |
| `w0t`, `w1t`... | 是否静态 IP |
| `w0i`, `w1i`... | 静态 IP |
| `w0g`, `w1g`... | 网关 |
| `w0m`, `w1m`... | 子网掩码 |
| `w0d`, `w1d`... | DNS |

旧版兼容字段：

| Key | 作用 |
|---|---|
| `wifi_ssid` | 旧版单 WiFi SSID |
| `wifi_pass` | 旧版单 WiFi 密码 |
| `wifi_static` | 旧版静态 IP 开关 |
| `wifi_ip` | 旧版静态 IP |
| `wifi_gw` | 旧版网关 |
| `wifi_mask` | 旧版子网掩码 |
| `wifi_dns` | 旧版 DNS |

### 更新设置

| Key | 作用 |
|---|---|
| `update_beta` | Beta 更新通道 |
| `auto_upd` | 自动更新开关 |

## 6. NVS namespace：gw

代码位置：

```text
include\web\dash_gateway.h
kDashGatewayPrefsNs = "gw"
```

保存 STA-AP 网关和 DNS 模式的小型状态。

| Key | 作用 |
|---|---|
| `en` | STA-AP 网关是否启用 |
| `mode` | DNS 黑名单 / 白名单模式 |
| `strict` | DNS strict 模式 |
| `profile` | DNS profile 版本 |
| `black` | 旧版黑名单迁移字段 |
| `white` | 旧版白名单迁移字段 |

注意：当前大体积 DNS 黑白名单正文主要不保存在 NVS，而是保存在 SPIFFS 文件中。

## 7. NVS namespace：can

代码位置：

```text
src\main.cpp
include\web\mcp2515_dashboard.h
canPrefs.begin("can", false)
```

保存 CAN 引脚设置。

| Key | 作用 |
|---|---|
| `tx` | 自定义 CAN TX GPIO |
| `rx` | 自定义 CAN RX GPIO |

如果 WebUI 中修改过 CAN 引脚，普通刷固件后仍会保留这些值。

## 8. SPIFFS 分区保存的文件

SPIFFS 分区地址：

```text
0x00820000 - 0x00FDFFFF
```

大小：

```text
0x7C0000
```

普通 `pio upload` 不会清除 SPIFFS。

当前项目主要保存：

| 文件 | 作用 |
|---|---|
| `/gw_black.txt` | DNS 黑名单 |
| `/gw_white.txt` | DNS 白名单 |
| `/gw_cidr.txt` | DNS CIDR 规则 |
| `/rec.csv` | CAN 录制文件 |

代码位置：

```text
include\web\dash_gateway.h
include\web\mcp2515_dashboard.h
```

## 9. app0 / app1 固件分区保存的内容

固件分区保存程序本体。

当前直接下载通常写入：

```text
app0: 0x00020000
```

固件里包含：

```text
程序代码
WebUI HTML/CSS/JS
默认参数
编译开关
内置 CAN 逻辑
FSD post-handler original-frame 注入逻辑
Legacy 1006 mux0 bit46 逻辑
HW3 1021 mux0 bit46 逻辑
HW3 mux2 custom-speed-only 逻辑
公告弹窗
按钮文字和页面布局
```

重要：固件里的默认值只有在 NVS 没有旧值时才生效。

例如：

```text
固件默认 apInjectionGate=false
但 NVS 里如果保存过 ap_gate=true
启动后仍会使用 NVS 的 true
```

## 10. otadata 分区

地址：

```text
0x00019000
```

作用：

```text
保存当前启动 app0 还是 app1
```

这不是用户配置，但影响设备从哪个固件槽启动。

## 11. RAM / PSRAM 中的运行时状态

以下内容只在运行时内存中，重启后会丢失：

```text
RX 计数
TX 计数
TX errors
mux0/mux1/mux2 统计
CAN 当前在线状态
APActive / Parked / Summoning 实时状态
当前速度限制原始值
last injected frame
DNS cache
DNS blocked 运行计数
Gateway NAT runtime 状态
WiFi 当前连接状态
CPU / heap / task 统计
日志 ring buffer
临时 CAN 录制缓冲
```

这些不在 NVS，也不在 SPIFFS。

## 12. 浏览器 localStorage

WebUI 还有一部分状态保存在手机或电脑浏览器里，不在 ESP32 板子上。

例如：

```text
主题 dark/light
语言
卡片展开 / 收起状态
小节展开 / 收起状态
CAN sniffer 显示模式
```

这些不会因为刷板子而改变，除非清浏览器缓存或换浏览器。

## 13. 不同刷写方式的影响

### 普通下载

命令：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload
```

通常更新：

```text
bootloader
partition table
otadata
app0 固件
```

通常保留：

```text
NVS 配置
SPIFFS 文件
浏览器 localStorage
```

### 清除后下载

命令：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t erase
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload
```

会清除：

```text
NVS
SPIFFS
otadata
app0/app1
coredump
所有 Flash 内容
```

刷完后所有配置回到固件默认。

### 16M 完整包从 0x0 写入

如果 16M 完整包包含整片 Flash 镜像，并从 `0x0` 写入，会覆盖：

```text
bootloader
partition table
NVS
otadata
app0
app1
SPIFFS
coredump
```

如果这个 16M 包里的 NVS/SPIFFS 区域是空白或默认镜像，则相当于清配置。

如果 16M 包里的 NVS/SPIFFS 区域来自某个已经配置过的板子，则会把那些配置一并复制过去。

### 不满 16M 的 bin 从 0x0 写入

不建议把普通 `firmware.bin` 从 `0x0` 写入。

原因：

```text
普通 firmware.bin 是 app 镜像，应该写到 app 分区地址，例如 0x20000。
从 0x0 写入会覆盖 bootloader 区域，导致无法启动。
```

## 14. 当前实用判断

如果只是改 WebUI、CAN 逻辑、按钮、默认值：

```text
普通 upload 足够
但 NVS 旧配置会继续生效
```

如果测试 AP Gate、FSD 开关、HW 模式、WiFi、DNS、CAN 引脚等默认行为：

```text
建议 erase + upload
```

如果要给别人一个完全一致的线刷包：

```text
需要生成完整 16M BIN
并明确它是否包含空白 NVS/SPIFFS 还是当前板子的 NVS/SPIFFS
```

## 15. 最关键结论

```text
普通下载：只换程序，不清配置。
清除下载：程序和配置都重来。
NVS：保存大多数 WebUI 设置和开关。
SPIFFS：保存 DNS 黑白名单、CIDR、CAN 录制文件。
RAM：保存实时状态，重启丢失。
ROM：不保存项目配置。
```
