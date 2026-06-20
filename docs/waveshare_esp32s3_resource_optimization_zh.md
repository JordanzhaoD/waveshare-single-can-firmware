# Waveshare ESP32-S3-RS485-CAN 硬件与资源优化建议

本文档针对本项目在 `waveshare_ESP32_S3_RS485_CAN` 环境下的硬件资源、Flash 分区、NVS/SPIFFS、PSRAM、内部 SRAM、FreeRTOS 任务和 WebUI/CAN 功能进行分析，并给出推荐优化方案。

项目路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\ev-open-can-tools-3.0.0-beta.5
```

相关资料：

- ESP32-S3 Datasheet: https://www.waveshare.net/w/upload/b/bd/Esp32-s3_datasheet_en.pdf
- ESP32-S3 Technical Reference Manual: https://www.waveshare.net/w/upload/1/11/Esp32-s3_technical_reference_manual_en.pdf
- Waveshare ESP32-S3-RS485-CAN Wiki: https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN
- Waveshare ESP32-S3-RS485-CAN 产品页: https://www.waveshare.com/product/esp32-s3-rs485-can.htm
- ESP-IDF ESP32-S3 Memory Types: https://docs.espressif.com/projects/esp-idf/en/release-v5.2/esp32s3/api-guides/memory-types.html
- ESP-IDF TWAI Driver: https://docs.espressif.com/projects/esp-idf/en/v4.4.1/esp32s3/api-reference/peripherals/twai.html

---

## 1. 当前硬件理解

Waveshare ESP32-S3-RS485-CAN 开发板适合本项目的原因：

- 使用 ESP32-S3 双核 Xtensa LX7，最高 240 MHz。
- 板载 16 MB Flash。
- 板卡型号通常为 ESP32-S3R8，具备 8 MB PSRAM；当前刷写日志也显示 `Embedded PSRAM 8MB`。
- 板载隔离 CAN、隔离 RS485、USB Type-C、WiFi、BLE。
- CAN 使用 ESP32-S3 内部 TWAI 控制器 + 外部 CAN 收发器。
- 支持 AP、STA、WebUI、OTA、插件存储、DNS/NAT 网关等功能。

需要注意：

- ESP32-S3 支持的是 Classical CAN/TWAI，不是 CAN-FD。
- ESP32-S3 内部 SRAM 有限，一部分还会被 cache、WiFi、系统任务占用。
- 板上 PSRAM 很大，但当前项目配置尚未启用 `CONFIG_SPIRAM`，所以 8 MB PSRAM 基本没有被利用。

---

## 2. 当前项目构建配置

当前环境位于：

```text
platformio.ini
```

关键配置如下：

```ini
[env:waveshare_ESP32_S3_RS485_CAN]
platform = ${espidf6.platform}
platform_packages = ${espidf6.platform_packages}
board = esp32s3box
framework = espidf
board_build.flash_size = 16MB
build_flags =
    -I include
    -DDRIVER_TWAI
    -DESP32_DASHBOARD
    -DTWAI_TX_PIN=GPIO_NUM_15
    -DTWAI_RX_PIN=GPIO_NUM_16
    -DPIN_LED=14
    -DDASH_STA_AP_GATEWAY
    -DESP_IDF_LWIP_HOOK_FILENAME=\"lwip_hooks.h\"
    -std=gnu++17
board_build.partitions = partitions_16mb_ota_4096k.csv
```

含义：

- 使用 TWAI 内置 CAN 控制器。
- CAN TX = GPIO15。
- CAN RX = GPIO16。
- LED = GPIO14。
- 启用 WebUI Dashboard。
- 启用 STA-AP Gateway / DNS 过滤功能。
- 启用 lwIP hook，用于严格 DNS/IP 转发过滤。
- 使用 16 MB Flash 分区表。

---

## 3. 当前 Flash 分区

当前分区文件：

```text
partitions_16mb_ota_4096k.csv
```

内容：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x400000,
app1,     app,  ota_1,   0x410000,0x400000,
spiffs,   data, spiffs,  0x810000,0x7E0000,
coredump, data, coredump,0xFF0000,0x10000,
```

换算：

| 分区 | 大小 | 用途 |
|---|---:|---|
| nvs | 20 KB | 小配置、WiFi、开关、模式 |
| otadata | 8 KB | OTA 状态 |
| app0 | 4 MB | OTA 固件槽 0 |
| app1 | 4 MB | OTA 固件槽 1 |
| spiffs | 约 7.875 MB | 插件、录制、过滤清单等文件 |
| coredump | 64 KB | 崩溃转储 |

当前编译结果：

```text
RAM:   41.8% 约 137 KB / 320 KB
Flash: 26.1% 约 1.096 MB / 4 MB app 分区
```

结论：

- Flash app 空间非常充足。
- SPIFFS 空间非常充足。
- NVS 只有 20 KB，偏小。
- 内部 SRAM 比 Flash 更值得优化。

---

## 4. 当前主要功能与资源压力

本项目在该开发板上启用的主要功能：

- CAN/TWAI 实时收发。
- 插件规则解析和注入。
- WebUI Dashboard。
- OTA 更新。
- WiFi AP + STA。
- STA-AP NAT 网关。
- DNS 黑白名单过滤。
- CAN Sniffer。
- CAN Recorder。
- Debug log。
- 插件 JSON 存储。
- 设置导入/导出。

主要资源压力来源：

| 功能 | 主要资源压力 | 建议位置 |
|---|---|---|
| CAN 实时收发 | 内部 SRAM、CPU Core 0 | 内部 SRAM |
| 插件运行规则 | 内部 SRAM、CPU | 内部 SRAM |
| WebUI | Flash、HTTP heap | Flash + 普通 heap |
| OTA | app 分区、HTTPS/TLS RAM | Flash app + heap |
| DNS 网关 | RAM、lwIP sockets | Core 1 + heap/PSRAM |
| 白名单/黑名单 | 持久化空间 | SPIFFS |
| CAN Recorder | 大 buffer | PSRAM 优先 |
| 插件 JSON | 文件存储 | SPIFFS |
| 日志 ring buffer | RAM | 内部 SRAM 或 PSRAM |

---

## 5. 已完成的白名单/过滤清单优化

之前 WEBUI 白名单重启丢失的根因：

- 白名单和黑名单原本作为长字符串写入 NVS。
- NVS 分区只有 20 KB。
- `Preferences::putString()` 写入失败没有向 WebUI 返回错误。
- 页面看到 `ok:true`，但实际 NVS 可能没有持久化成功。
- 重启后从 NVS 读不到新值，于是看起来像丢失。

已经完成的优化：

- 黑名单保存到 SPIFFS：

```text
/gw_black.txt
```

- 白名单保存到 SPIFFS：

```text
/gw_white.txt
```

- NVS 只保存小配置：

```text
enabled / mode / strict
```

- 保存失败时会返回：

```json
{"ok":false,"error":"save failed"}
```

- 前端保存后减少重复请求，避免保存响应被多次轮询拖慢。

修改涉及：

```text
include/web/dash_gateway.h
include/web/mcp2515_dashboard_ui.src.h
include/web/mcp2515_dashboard_ui.h
```

---

## 6. 推荐 Flash 分区优化

当前 app 分区 4 MB 足够，但 NVS 20 KB 偏小。建议新增一个优化分区表：

```csv
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x10000,
otadata,  data, ota,     0x19000, 0x2000,
app0,     app,  ota_0,   0x20000, 0x400000,
app1,     app,  ota_1,   0x420000,0x400000,
spiffs,   data, spiffs,  0x820000,0x7C0000,
coredump, data, coredump,0xFE0000,0x20000,
```

优化点：

| 分区 | 当前 | 建议 | 原因 |
|---|---:|---:|---|
| NVS | 20 KB | 64 KB | 避免 WiFi、多网络、开关、插件状态、网关状态挤爆 |
| app0 | 4 MB | 4 MB | 当前固件约 1.1 MB，足够 |
| app1 | 4 MB | 4 MB | 保留完整双 OTA |
| SPIFFS | 约 7.875 MB | 约 7.75 MB | 仍然很大，足够插件和清单 |
| coredump | 64 KB | 128 KB | 更利于崩溃分析 |

不建议现在把 app 分区扩大到 5 MB 或 6 MB，因为当前固件只用了约 26%。

如果将来固件超过 3 MB，再考虑：

```text
app0/app1 = 5 MB
SPIFFS = 约 5.75 MB
```

---

## 7. NVS 与 SPIFFS 分工原则

### NVS 适合保存

- WiFi AP SSID/password。
- WiFi STA 多网络配置。
- CAN pins。
- hardware mode。
- dashboard flags。
- plugin enabled state。
- gateway enabled/mode/strict。
- OTA/update flags。

### SPIFFS 适合保存

- 插件 JSON。
- DNS 黑名单。
- DNS 白名单。
- CAN recorder CSV。
- 设置备份文件。
- 大日志文件。
- 将来可能添加的规则库、模板文件。

### 不建议放入 NVS

- 长白名单。
- 长黑名单。
- 大 JSON。
- CAN recorder 数据。
- 高频变化的 blocked domains。
- 大量日志。

---

## 8. PSRAM 优化建议

当前板上有 8 MB PSRAM，但项目尚未启用。建议启用 PSRAM。

通过：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t menuconfig
```

进入：

```text
Component config
  -> ESP PSRAM
    -> Support for external, SPI-connected RAM
```

启用后确认配置中出现：

```text
CONFIG_SPIRAM=y
```

注意：

- 不建议盲目手写 OPI/QSPI 参数。
- 以 menuconfig 自动识别和板卡实际硬件为准。

### 优先迁移到 PSRAM 的对象

1. CAN Recorder buffer

当前：

```cpp
static RecFrame recBuf[REC_CAP];
```

建议改成运行时分配：

```cpp
recBuf = static_cast<RecFrame *>(
    heap_caps_calloc(REC_CAP, sizeof(RecFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
);
```

优点：

- 不录制时可以不分配。
- 内部 SRAM 减少约 40 KB 压力。
- 启用 PSRAM 后可把 `REC_CAP` 提高到 4000 或 8000。

2. DNS gateway 大缓存

可考虑迁移：

- `gatewayPendingQueries`
- `gatewayAllowedIps`
- `gatewayBlockedIps`

但 DNS 包处理的 `rx[512]` / `tx[512]` 栈缓冲建议保留内部 RAM。

3. 插件编辑临时内容

插件运行时精简规则保留内部 SRAM，插件原始 JSON/编辑器临时内容可放 PSRAM 或 SPIFFS。

### 不建议放入 PSRAM 的对象

- TWAI RX/TX 关键路径。
- CAN 注入当前帧。
- ISR/driver 需要低延迟访问的数据。
- 高优先级 CAN 任务栈。
- 插件运行时正在使用的热路径状态。

原因：PSRAM 通过 cache 访问，延迟高于内部 SRAM；CAN 实时路径应该优先使用内部 SRAM。

---

## 9. FreeRTOS 任务与 CPU 核心分配

当前项目任务分配基本合理：

| 任务 | 当前核心 | 说明 |
|---|---:|---|
| CAN 实时任务 `can_rt` | Core 0 | 高优先级，实时处理 CAN |
| WiFi task | Core 1 | 网络栈 |
| TCP/IP task | Core 1 | lwIP |
| Web task | Core 1 | WebUI/HTTP |
| DNS gateway task | Core 1 | DNS/NAT 过滤 |

建议继续保持：

```text
Core 0: CAN 实时收发、注入、过滤、插件运行核心逻辑
Core 1: WiFi、WebServer、DNS/NAT、OTA、日志页面
```

当前相关代码：

```text
src/main.cpp
include/web/dash_gateway.h
include/web/mcp2515_dashboard.h
```

### 任务栈建议

当前栈大小：

| Task | 当前 | 可测试目标 | 说明 |
|---|---:|---:|---|
| main | 12288 | 8192 | 启动后主循环较轻，可测试降低 |
| can_rt | 6144 | 4096/5120 | 取决于插件和日志路径 |
| web | 8192 | 6144 | 如果经常导入导出 JSON，保留 8192 |
| gw_dns | 6144 | 4096 | DNS 逻辑可测试降低 |
| tcpip | 3072 | 保持 | 不建议动 |

建议先通过 WebUI 的 Task Load / stack watermark 观察，再逐步降低。

不要一次性大幅降低任务栈。

---

## 10. CAN/TWAI 优化建议

Waveshare 这个环境使用 ESP32-S3 内部 TWAI，不是 MCP2515。

建议：

- CAN 实时任务继续 pin 到 Core 0。
- WiFi/Web/DNS 继续 pin 到 Core 1。
- 优先使用 TWAI hardware acceptance filter。
- 对 sparse filter 的 false positive，再做软件精确过滤。
- 插件 JSON 必须预解析，CAN 热路径不要做 JSON/String 操作。
- CAN recorder 只在用户开启录制时写 buffer。
- 录制导出写 SPIFFS/生成 CSV 时不要阻塞 CAN 实时路径。

Recorder 优化建议：

- 默认 `REC_CAP=512` 或 `1000`，降低内部 SRAM 压力。
- 启用 PSRAM 后，允许 `REC_CAP=4000/8000`。
- 录制开启时分配，停止后释放。

---

## 11. WiFi / NAT / DNS 优化建议

当前启用：

```ini
-DDASH_STA_AP_GATEWAY
-DESP_IDF_LWIP_HOOK_FILENAME=\"lwip_hooks.h\"
```

当前 sdkconfig 中已启用：

```ini
CONFIG_LWIP_IP_FORWARD=y
CONFIG_LWIP_IPV4_NAPT=y
CONFIG_LWIP_MAX_SOCKETS=16
```

建议：

- `CONFIG_LWIP_MAX_SOCKETS=16` 保持即可。
- DNS 白名单/黑名单继续放 SPIFFS。
- DNS blocked domain 记录只放 RAM，不持久化。
- DNS allowed/blocked IP cache 可迁移到 PSRAM。
- 页面隐藏时暂停后台轮询，降低 WebServer 负载。
- 保存 DNS 清单时不要立刻发多个刷新请求；当前已优化。

---

## 12. WebUI 与 OTA 优化建议

当前 WebUI 已 gzip 压缩，`DASH_HTML_GZ` 约 51 KB，放在 Flash rodata 中，不是 RAM 主要压力。

建议保持：

- WebUI gzip 内嵌。
- OTA 双 app 分区。
- SPIFFS 用于插件和文件。

如果将来固件体积明显增大，可考虑：

- 移除未使用的 WebUI 面板。
- 将部分 WebUI 静态资源放 SPIFFS，而不是全部编译进固件。
- 禁用不需要的 HTTPS OTA/mbedTLS 功能，但会影响在线更新能力。

当前不建议过早拆分 WebUI，因为 app 分区空间充足。

---

## 13. 推荐最终资源布局

| 资源 | 推荐位置 |
|---|---|
| 固件 app0/app1 | Flash OTA 分区，各 4 MB |
| WebUI gzip | Flash rodata |
| 小配置 | NVS 64 KB |
| WiFi 凭据 | NVS |
| CAN pins/hardware mode | NVS |
| 插件 JSON | SPIFFS |
| 插件运行规则 | 内部 SRAM，精简结构 |
| DNS 黑名单/白名单 | SPIFFS |
| DNS 运行缓存 | PSRAM 优先，内部 RAM fallback |
| CAN recorder buffer | PSRAM，开启时分配 |
| CAN 实时 frame/state | 内部 SRAM |
| TWAI driver queue | 内部 SRAM |
| 日志 ring buffer | 内部 SRAM 或 PSRAM |
| coredump | Flash 128 KB |

---

## 14. 推荐实施顺序

建议按以下顺序推进：

1. 新增 16 MB 优化分区表，NVS 扩到 64 KB，coredump 扩到 128 KB。
2. 启用 PSRAM。
3. 把 CAN recorder buffer 改成运行时 PSRAM 分配。
4. 把 DNS gateway 大缓存改成 PSRAM 优先分配。
5. Recorder 改成不开启不分配，停止后释放。
6. WebUI 页面隐藏时暂停后台轮询。
7. 用 WebUI Task Load 观察任务栈余量。
8. 逐步下调 `main/web/gw_dns/can_rt` 栈大小。
9. 保持 CAN 在 Core 0，WiFi/Web/DNS 在 Core 1。
10. 压测：CAN 高流量 + WebUI 打开 + DNS 网关启用 + 保存白名单 + OTA 检查。

---

## 15. 建议新增分区文件

建议新增：

```text
partitions_16mb_ota_4096k_nvs64.csv
```

内容：

```csv
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x10000,
otadata,  data, ota,     0x19000, 0x2000,
app0,     app,  ota_0,   0x20000, 0x400000,
app1,     app,  ota_1,   0x420000,0x400000,
spiffs,   data, spiffs,  0x820000,0x7C0000,
coredump, data, coredump,0xFE0000,0x20000,
```

然后在 `platformio.ini` 中修改：

```ini
board_build.partitions = partitions_16mb_ota_4096k_nvs64.csv
```

注意：

- 修改分区表后，建议串口完整刷写一次。
- 不建议只通过 OTA 切换分区表。
- 刷写前建议导出 WebUI 设置备份。

---

## 16. 建议验证清单

完成优化后，建议验证：

- WebUI 能正常打开。
- AP 模式正常。
- STA 连接正常。
- NAT 网关正常。
- DNS 黑白名单保存后重启仍存在。
- 白名单保存速度比之前更快。
- 插件安装、启用、重启后状态正常。
- CAN sniff 正常。
- CAN injection 正常。
- CAN recorder 开启/停止/导出正常。
- OTA 手动上传正常。
- WebUI System Health 里 heap、stack、PSRAM 状态正常。
- 高 CAN 流量下 WebUI 打开不影响实时任务。

---

## 17. 总结

当前硬件足够强，Flash 空间也非常宽裕。真正应该优化的是：

1. 内部 SRAM 使用。
2. NVS 容量和写入可靠性。
3. 大 buffer 从静态内部 SRAM 迁移到 PSRAM。
4. 文件型数据从 NVS 迁移到 SPIFFS。
5. CAN 实时任务和 WiFi/Web/DNS 任务继续分核运行。

最优方向：

```text
小配置进 NVS，大文件进 SPIFFS，大缓冲进 PSRAM，CAN 实时路径留内部 SRAM。
```
