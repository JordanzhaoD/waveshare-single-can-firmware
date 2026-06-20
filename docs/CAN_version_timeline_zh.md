# IDF 3.0.0 CAN/FSD 修改版本时间线

本文档按照聊天记录、备份目录和代码检查结果，记录 `IDF-DNS-AP-NAT` 项目在 FSD/CAN 相关修改过程中的每个版本、修改内容、CAN 逻辑差异和可回退位置。

当前日期：2026-05-14

## 目录命名说明

实际本地目录使用的是带连字符的名称：

- `C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\CAN-修改前备份`
- `C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0-稳定激活版本`

聊天中有时写成 `CAN 修改前备份`、`IDF-3.0.0 稳定激活版本`，本文统一用实际目录名。

## 核心术语

- `1021 mux0`：FSD 激活核心子帧，稳定版只设置 bit46。
- `1021 mux1`：原 3.0.0 中用于 nag/ready 辅助，常见操作是清 bit19。
- `1021 mux2`：原 3.0.0 中用于 offset/cap/readiness/custom speed 辅助。
- `original frame`：handler 修改前保存的真实车端原始 CAN 帧。
- `post-handler`：handler 运行后再执行的注入阶段。
- `AP Gate`：注入门控，稳定版中只有满足车辆状态时才真正发 CAN 修改帧。

---

## 0. 初始当前项目

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT
```

最初分析对象是当前 IDF 3.0.0 项目，重点包括：

- CAN 总线功能
- WiFi AP + NAT
- DNS 黑白名单/过滤
- WebUI 配置保存
- FSD 激活 CAN 帧逻辑
- ESP32-S3/Waveshare 硬件配置

当时当前项目更偏向 3.0.0 内置 handler 模型：

```text
read frame
  -> handler 内部解析/修改/发送
```

不是 2.5.2 插件的 original-frame post-handler 模型。

---

## 1. 早期备份：IDF-DNS-AP-NAT_backup_20260513_203154

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT_backup_20260513_203154
```

创建时间：2026-05-13 20:31

状态：

- 早期 IDF 项目备份。
- 还没有后续 FSD 稳定激活相关的 AP Gate / post-handler 修改。
- 可作为较早 WebUI/NAT/DNS/CAN 状态参考。

核心特征：

```text
无 DASH_FSD_252_COMPAT=1
无 dashPostProcessFrame()
无 CanFrame original = frame
无新增 AP Gate
```

---

## 2. CAN 修改前备份

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\CAN-修改前备份
```

创建时间：2026-05-13 21:21

创建原因：

- 在正式修改 CAN/FSD 逻辑前，用户要求先做备份，名称为 `CAN 修改前备份`。

状态：

- 这是后续所有 FSD 时序修改之前的基准版本。
- 当前主工程在 2026-05-14 08:08 左右曾恢复到这个版本，并下载到板子。

核心 CAN 逻辑：

```text
read frame
  -> handler.handleMessage(frame)
  -> handler 内部直接修改/发送
```

关键特征：

- 没有 `DASH_FSD_252_COMPAT=1`。
- 没有 `dashPostProcessFrame()`。
- 没有 `CanFrame original = frame`。
- 没有新增 `dashApInjectionAllowed()`。
- `dashInjectionActive()` 基本回到 CAN 修改前逻辑。
- HW3 handler 内仍按原 3.0.0 方式处理 mux0/mux1/mux2。

mux 行为：

```text
mux0：handler 内直接处理 FSD 激活/速度相关逻辑
mux1：handler 内可清 bit19 并发送
mux2：handler 内可执行 offset/cap/readiness/custom speed assist
```

优点：

- 不会因为新增 AP Gate 而显示 `Waiting AP`。
- 更接近原 3.0.0 行为。

缺点：

- 总线干预更多。
- FSD 激活发送时序不如 2.5.2 插件路径保守。
- 注入可能更早发生，不一定基于 handler 前 original 原始帧。

---

## 3. 第一次稳定激活修改：向 2.5.2 时序靠近

修改目标：

```text
让 3.0.0 的 CAN 获取/修改/发送更接近 2.5.2 插件逻辑：
read frame
  -> 保存 original
  -> handler 更新状态
  -> post-handler 使用 original 原始帧注入
```

核心设计：

```text
用户 Turn FSD On
    ↓
canActive = true
    ↓
持续监听真实车端帧
    ↓
保存 original
    ↓
handler 更新 APActive / Parked / Summoning / ADEnabled 等状态
    ↓
AP Gate 判断
    ↓
允许：如果 original 是 1021 mux0，复制 original，只改 bit46，发送
不允许：继续监听，不发送
```

### 3.1 修改 `include/app.h`

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT\include\app.h
```

修改内容：

```cpp
CanFrame original = frame;
h->handleMessage(frame, *appDriver);
dashPostProcessFrame(original, *appDriver);
```

效果：

- handler 运行前保存真实车端原始帧。
- handler 先更新车辆状态。
- handler 后再执行 FSD 注入。
- 注入基于 original，而不是 handler 内可能已修改过的 frame。

### 3.2 修改 `include/web/mcp2515_dashboard.h`：新增 AP Gate

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT\include\web\mcp2515_dashboard.h
```

新增逻辑：

```cpp
static bool dashApInjectionAllowed()
{
    return dashHandler && dashHandler->injectionGateOpen();
}

static bool dashInjectionActive()
{
    return canActive && dashApInjectionAllowed();
}
```

门控来自 handler 的车辆状态判断，大致为：

```text
APActive || Parked || Summoning
```

效果：

- `Turn FSD On` 只是 armed。
- AP Gate 不允许时不发送 CAN 修改帧。
- AP Gate 允许时才进入真正注入。

### 3.3 修改 `include/web/mcp2515_dashboard.h`：新增 post-handler 注入

新增函数：

```cpp
static void dashPostProcessFrame(const CanFrame &original, CanDriver &driver)
```

核心逻辑：

```text
只在 DASH_FSD_252_COMPAT 下执行
只在 HW3 下执行
必须 dashInjectionActive() 为 true
只匹配 original 1021 mux0
复制 original
只设置 bit46 = 1
modified == original 时不发送
modified != original 时 driver.send(modified)
```

效果：

- 更接近 2.5.2 插件路径。
- 总线干预最小。
- FSD 激活帧基于原始车端帧。

### 3.4 修改 `include/handlers.h`

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT\include\handlers.h
```

新增宏：

```cpp
#ifndef DASH_FSD_252_COMPAT
#define DASH_FSD_252_COMPAT 0
#endif
```

兼容模式行为：

```text
mux0：handler 不再直接发送 bit46，真正发送交给 dashPostProcessFrame(original)
mux1：不发送
mux2：不发送
```

目标：

- 减少总线干预。
- 保留 handler 的状态更新能力。
- 将 FSD 激活输出集中到 post-handler original-frame 路径。

### 3.5 修改 `platformio.ini`

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT\platformio.ini
```

Waveshare 环境增加：

```ini
-DDASH_FSD_252_COMPAT=1
```

效果：

- 编译启用 2.5.2 风格兼容路径。
- 启用 post-handler original-frame bit46-only。
- 禁用兼容模式下 mux1/mux2 发送。

---

## 4. IDF-3.0.0-稳定激活版本

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0-稳定激活版本
```

创建时间：2026-05-13 23:32

备份原因：

- 用户在测试该状态可以稳定出现灰色方向盘后，要求备份为 `IDF-3.0.0 稳定激活版本`。

代码检查结果：

- 核心 CAN 修改完整存在。
- 不是明显缺文件或损坏状态。
- 但聊天截图显示当时界面只列出 3 个文件修改：

```text
include/app.h
include/web/mcp2515_dashboard.h
include/handlers.h
```

而后续检查发现稳定版与 CAN 修改前还存在：

```text
platformio.ini
include/web/mcp2515_dashboard_ui.src.h
include/web/mcp2515_dashboard_ui.h
```

差异。因此备份目录可能不是用户截图中“刚完成修改那一刻”的完全精确状态，尤其 UI/生成文件可能后续变动过。

稳定版 CAN 逻辑：

```text
-DASH_FSD_252_COMPAT=1

read frame
  -> CanFrame original = frame
  -> handler.handleMessage(frame)
  -> dashPostProcessFrame(original)

mux0：AP Gate 允许后，复制 original 1021 mux0，只改 bit46，changed 才发送
mux1：不发送
mux2：不发送
```

预期现象：

```text
RX 增长
FSD Switch On 后进入 armed
AP Gate 未开时可能显示 Waiting AP
AP Gate 允许后 TX 增长，发送 mux0 bit46
```

用户实测历史：

- 用户明确表示该版本在多次测试中能稳定出现灰色方向盘。
- 后续再次刷回时出现 Waiting AP，因此怀疑备份环节或 NVS/测试状态差异。

---

## 5. mux1/mux2 测试版

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0 mux测试版备份-20260514-004320
```

创建时间：2026-05-14 00:43

创建原因：

- 在稳定版基础上，为测试 mux1/mux2 是否能改善灰轮闪烁、不保持、自定义限速等问题，恢复 mux1/mux2 测试功能。

新增编译宏：

```ini
-DDASH_FSD_252_COMPAT=1
-DDASH_FSD_252_MUX_ASSIST=1
```

CAN 逻辑：

```text
mux0：仍保持 post-handler original-frame bit46-only
mux1：AP Gate 允许后恢复清 bit19 并发送
mux2：AP Gate 允许后恢复 readiness/offset/cap assist
```

特点：

- 不再是严格 2.5.2 bit46-only。
- 是“2.5.2 时序 + 3.0.0 mux1/mux2 assist”的混合测试版。
- 总线干预比稳定版多。

用户实测：

- 后续用户反馈该版本以及后续版本出现 `Waiting AP`，灰色方向盘没有出现。

---

## 6. mux2 custom-only / 诊断版

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0 mux2诊断版备份-20260514-012604
```

创建时间：2026-05-14 01:26

创建原因：

- 用户希望只恢复 mux2 custom speed，不恢复 mux1，不恢复 mux2 readiness。
- 目标是恢复 HW3 自定义限速，同时尽量不破坏 FSD 稳定激活。

新增编译宏：

```ini
-DDASH_FSD_252_COMPAT=1
-DDASH_FSD_252_MUX2_CUSTOM_ONLY=1
```

CAN 逻辑：

```text
mux0：post-handler original-frame bit46-only
mux1：不发送
mux2：只尝试 custom speed 相关逻辑，不恢复完整 readiness assist
```

用户实测：

- 仍出现 Waiting AP / TX 为 0 等现象。
- 后续没有作为稳定方向继续。

---

## 7. 回退 mux 测试前备份

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT-回退mux测试前备份-20260514-074230
```

创建时间：2026-05-14 07:42

创建原因：

- 用户要求回退到 mux1/mux2 测试版前，先备份当时当前工程状态。

状态：

- 该状态本质上是 `AP Gate + post-handler original-frame + mux0 bit46-only`。
- 不包含 mux assist。

关键特征：

```text
-DASH_FSD_252_COMPAT=1
post-handler original-frame
AP Gate
mux1 不发送
mux2 不发送
```

---

## 8. mux 测试版回退前备份

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT-mux测试版回退前备份-20260514-074911
```

创建时间：2026-05-14 07:49

创建原因：

- 用户要求从 mux 测试版恢复回稳定 bit46-only 状态前，先备份 mux 测试版。

状态：

```text
-DASH_FSD_252_COMPAT=1
-DASH_FSD_252_MUX_ASSIST=1
post-handler original-frame
AP Gate
mux0 bit46-only
mux1/mux2 assist 开启
```

---

## 9. 恢复 CAN 修改前之前的备份

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT-恢复CAN修改前前备份-20260514-080749
```

创建时间：2026-05-14 08:07

创建原因：

- 用户要求恢复到 CAN 修改前版本之前，先备份当时当前工程。

状态：

```text
-DASH_FSD_252_COMPAT=1
AP Gate
post-handler original-frame
mux0 bit46-only
mux1 不发送
mux2 不发送
```

之后执行：

```text
从 CAN-修改前备份 恢复到 IDF-DNS-AP-NAT
构建成功
下载到板子成功
```

---

## 10. 当前主工程状态：IDF-DNS-AP-NAT

路径：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT
```

截至本文档创建时，主工程状态：

```text
已恢复为 CAN-修改前备份 状态
```

关键特征：

```text
无 DASH_FSD_252_COMPAT=1
无 dashPostProcessFrame()
无新增 AP Gate
无 original-frame post-handler 注入
```

并且该 CAN 修改前版本曾被下载到板子。

---

## 11. 从 CAN 修改前重建稳定激活版

中文路径版本：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\CAN修改前-重建稳定激活版-20260514-091431
```

ASCII 路径版本：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0-stable-rebuild-20260514-091431
```

创建原因：

- 用户怀疑 `IDF-3.0.0-稳定激活版本` 备份过程中可能中断，要求：
  1. 检查代码。
  2. 从 `CAN-修改前备份` 复制一份，再按稳定版沟通和代码修改记录重做修改。

执行过程：

1. 从 `CAN-修改前备份` 复制出中文路径重建版。
2. 在复制版本中重建稳定激活核心修改。
3. 中文路径下 PlatformIO 在 `Reading CMake configuration...` 阶段失败，未输出明确错误，判断可能与 ESP-IDF/CMake 对中文路径处理有关。
4. 再复制到 ASCII 路径：`IDF-3.0.0-stable-rebuild-20260514-091431`。
5. ASCII 路径下构建成功。

重建版核心逻辑：

```text
-DASH_FSD_252_COMPAT=1

read frame
  -> 保存 original
  -> handler.handleMessage(frame)
  -> dashPostProcessFrame(original)

mux0：HW3 + AP Gate 允许后，复制 original 1021 mux0，只改 bit46
mux1：不发送
mux2：不发送
```

构建结果：

```text
pio run -e waveshare_ESP32_S3_RS485_CAN
SUCCESS
```

固件位置：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0-stable-rebuild-20260514-091431\.pio\build\waveshare_ESP32_S3_RS485_CAN\firmware.bin
```

随后用户选择方案 2：

```text
erase + upload
```

已执行：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t erase
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload
```

结果：

```text
erase SUCCESS
upload SUCCESS
```

当前板子刷入：

```text
干净 NVS + IDF-3.0.0-stable-rebuild-20260514-091431
```

---

## 12. 版本差异总表

| 版本 | 路径 | 核心宏 | mux0 | mux1 | mux2 | AP Gate | original post-handler | 备注 |
|---|---|---|---|---|---|---|---|---|
| 早期备份 | `IDF-DNS-AP-NAT_backup_20260513_203154` | 无 | 原 3.0.0 | 原 3.0.0 | 原 3.0.0 | 无新增 | 无 | 早期参考 |
| CAN 修改前 | `CAN-修改前备份` | 无 | handler 内直接处理 | 可发送 | 可发送 | 无新增 | 无 | 当前主工程曾恢复到此版本 |
| 稳定激活版 | `IDF-3.0.0-稳定激活版本` | `DASH_FSD_252_COMPAT=1` | original bit46-only | 不发送 | 不发送 | 有 | 有 | 用户曾测试可稳定灰轮 |
| mux 测试版 | `IDF-3.0.0 mux测试版备份-20260514-004320` | `DASH_FSD_252_COMPAT=1`, `DASH_FSD_252_MUX_ASSIST=1` | original bit46-only | AP Gate 后发送 | AP Gate 后 assist | 有 | 有 | 测试 mux1/mux2 |
| mux2 诊断版 | `IDF-3.0.0 mux2诊断版备份-20260514-012604` | `DASH_FSD_252_COMPAT=1`, `DASH_FSD_252_MUX2_CUSTOM_ONLY=1` | original bit46-only | 不发送 | custom-only | 有 | 有 | 测试自定义限速 |
| bit46-only 回退备份 | `IDF-DNS-AP-NAT-回退mux测试前备份-20260514-074230` | `DASH_FSD_252_COMPAT=1` | original bit46-only | 不发送 | 不发送 | 有 | 有 | 回退 mux 测试前备份 |
| mux 回退前备份 | `IDF-DNS-AP-NAT-mux测试版回退前备份-20260514-074911` | `DASH_FSD_252_COMPAT=1`, `DASH_FSD_252_MUX_ASSIST=1` | original bit46-only | AP Gate 后发送 | AP Gate 后 assist | 有 | 有 | 从 mux 测试版回退前备份 |
| 恢复 CAN 前备份 | `IDF-DNS-AP-NAT-恢复CAN修改前前备份-20260514-080749` | `DASH_FSD_252_COMPAT=1` | original bit46-only | 不发送 | 不发送 | 有 | 有 | 恢复 CAN 修改前之前的备份 |
| 重建稳定版 | `IDF-3.0.0-stable-rebuild-20260514-091431` | `DASH_FSD_252_COMPAT=1` | original bit46-only | 不发送 | 不发送 | 有 | 有 | 从 CAN 修改前重建，已 erase+upload |
| **APGate 旁路稳定版** | `IDF-3.0.0-APGate旁路稳定版-20260514-163830` | `DASH_FSD_252_COMPAT=1` + 新增 `apInjectionGate` 默认 false | original bit46-only | 不发送 | 不发送 | **可关闭，默认旁路** | 有 | **A/B 反向验证根因：D 档灰轮稳定** |
| 当前主工程 | `IDF-DNS-AP-NAT` | `DASH_FSD_252_COMPAT=1` + apInjectionGate 默认 false | original bit46-only | 不发送 | 不发送 | 可关闭，默认旁路 | 有 | 与 APGate 旁路稳定版同步 |

---

## 13. A/B 反向验证 · 2026-05-14 16:38 ⭐

### 假设
3.0 灰轮不稳定的根因是 `dashApInjectionAllowed()` 收紧 —— 2.5.2 有 `!apInjectionGate || injectionGateOpen()` 的旁路开关（默认旁路），3.0 把旁路删掉只剩 `injectionGateOpen()`，导致 D 档行驶时 `Parked=false` 直接关闭注入。

### 改动
[include/web/mcp2515_dashboard.h](../include/web/mcp2515_dashboard.h) 仅 5 处：

1. 新增 `static bool apInjectionGate = false` + `volatile uint32_t lastInjectMs = 0`
2. `dashApInjectionAllowed()` 改回 2.5.2 风格：`!apInjectionGate || injectionGateOpen()`
3. `dashPostProcessFrame` 发送成功后写 `lastInjectMs = millis()`
4. NVS save/load 加 `"ap_gate"` 键，默认 `false`
5. `/status` JSON 新增 `apGateEnabled` 和 `lastInjectMs` 字段

**没动**：handlers.h / app.h / mux1 / mux2 / CAN 任务 / TWAI 队列 / DASH_FSD_252_COMPAT。

### 验证

| 试验 | `apInjectionGate` | D 档 `apGateOpen` | D 档 `ia` | D 档 `mux[0].tx` | D 档灰轮 |
|---|---|---|---|---|---|
| Forward 正向 | `false`（旁路） | true（短路） | true | 增长 549/733 | **稳定** ✅ |
| Reverse 反向 | `true`（门控） | **false**（Parked=false） | **false** | **停在 49** | **消失/无法出现** ✅ |

只翻一个变量，结果完全相反 → **根因坐实，无歧义**。

### 物理旁证（reverse 版 D 档下 probe 抓帧）

```
probe.rx (网关原始)  : [0,0,0,208,32, 1,2,128]  byte 5=0x01 → bit46=0
probe.tx (本想注入)  : [0,0,0,208,32,65,2,128]  byte 5=0x41 → bit46=1
```

网关本来就广播 bit46=0；reverse 版 `ia=false` 不发，ECU 一直看到 0 → 灰轮不出现。

### 已知遗留

- `test/test_wifi_settings_regression.py` 期望 `ap-gate-tgl` UI 元素 + `hasArg("apg")` POST 端点，本版未加（用编译期切换做 A/B 验证）。如需产品化，下一轮补 UI。


---

## 13. 关键结论

1. `CAN-修改前备份` 是所有 FSD 时序修改前的基准。
2. `IDF-3.0.0-稳定激活版本` 的核心 CAN 修改完整存在，不像是核心代码缺失。
3. 稳定版核心是：

```text
AP Gate + post-handler original-frame + 1021 mux0 bit46-only + mux1/mux2 不发送
```

4. mux 测试版是在稳定版基础上增加 mux1/mux2 assist：

```text
DASH_FSD_252_MUX_ASSIST=1
```

5. mux2 诊断版是在稳定版基础上只尝试 mux2 custom speed：

```text
DASH_FSD_252_MUX2_CUSTOM_ONLY=1
```

6. 后续为了排除备份中断/路径/配置问题，已经从 `CAN-修改前备份` 重建出 ASCII 路径稳定版：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0-stable-rebuild-20260514-091431
```

7. 该重建稳定版已构建成功，并在 erase 后下载到板子。

---

## 14. 回退建议

如需恢复不同状态：

### 恢复 CAN 修改前

源目录：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\CAN-修改前备份
```

用途：

```text
恢复原 3.0.0 handler 内直接发送 mux0/mux1/mux2 逻辑
```

### 恢复稳定 bit46-only

优先使用重建版：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0-stable-rebuild-20260514-091431
```

用途：

```text
AP Gate + post-handler original-frame + mux0 bit46-only + mux1/mux2 不发送
```

### 恢复 mux1/mux2 测试版

源目录：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0 mux测试版备份-20260514-004320
```

用途：

```text
稳定版时序 + mux1/mux2 assist 测试
```

### 恢复 mux2 custom-only 诊断版

源目录：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-3.0.0 mux2诊断版备份-20260514-012604
```

用途：

```text
稳定版时序 + 只测试 mux2 custom speed
```
