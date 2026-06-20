# 恢复 2.5.2 HW 选择与插件功能工作清单

生成时间：2026-05-14  
当前底座：`IDF稳定灰轮版` / `IDF-DNS-AP-NAT` 当前工程  
参考版本：

- `C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\ev-2.5.2`
- `C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\ev-open-can-tools-3.0.0-beta.5-ooo`

## 0. 总目标

在不破坏当前“稳定灰轮版”的前提下，评估并逐步恢复：

1. 2.5.2 风格的 `HW3 / HW4 / LEGACY` 硬件选择能力。
2. 2.5.2 风格的 JSON 插件系统。
3. 插件基于原始车端帧执行的 CAN 时序。
4. WebUI 插件管理能力。

当前稳定灰轮版的核心行为必须保持：

```text
read frame
  -> 保存 original
  -> handler.handleMessage(frame)
  -> post-handler original-frame 注入
  -> HW3 1021 mux0 bit46-only
  -> AP Gate 默认旁路 apInjectionGate=false
  -> mux1/mux2 不发送
```

## 1. 参考版本定位

### 1.1 2.5.2 的意义

`ev-2.5.2` 作为行为标准：

- 插件执行时序：`read -> original -> handler -> plugin(original)`。
- FSD 激活插件规则：`1021 mux0 set_bit 46 = 1`。
- AP Gate 语义：`!apInjectionGate || injectionGateOpen()`。
- 插件基于 handler 修改前的原始车端帧。
- 插件规则 ID 会合并进 CAN filter。

### 1.2 3.0.0-beta.5-ooo 的意义

`ev-open-can-tools-3.0.0-beta.5-ooo` 作为移植模板：

- 更接近当前 ESP-IDF / 3.0 工程结构。
- 已有 `plugin_engine.h`。
- 已有 WebUI `.src.h` 插件面板和 Plugin Editor。
- 已有后端插件 API。
- 已有 `pluginGetFilterIds()`、`pluginProcessFrame()`、`pluginLoadAll()`。

### 1.3 当前工程状态

当前工程已有：

- `hwMode`。
- `/config hw=0/1/2`。
- `dashSwapHandler(hwMode)`。
- HW3/HW4/LEGACY handler。
- NVS 保存 `hw`。
- 当前稳定灰轮注入逻辑。

当前工程缺失：

- `include/plugin_engine.h`。
- 插件 WebUI 面板。
- 插件 API 路由。
- 插件 SPIFFS 管理。
- 插件 filter 合并。
- 插件测试 API。

## 2. 风险原则

### 2.1 禁止破坏的稳定点

以下行为不应被默认改变：

- `apInjectionGate=false` 默认旁路。
- HW3 默认模式。
- 当前稳定灰轮 `1021 mux0 bit46-only` 注入。
- mux1/mux2 默认不发送。
- WebUI DNS/STA-AP 保存修复保留。
- `FSD 关闭` 文本保留。

### 2.2 插件与内置注入冲突风险

如果同时启用：

```text
内置稳定 bit46 注入
插件 1021 mux0 bit46 注入
```

可能出现：

- 双发注入帧。
- TX 计数难以判断。
- 实车效果难以归因。
- AP Gate / 插件门控 / 内置门控混杂。

建议设计成互斥模式：

```text
FSD Activation Mode:
  1. Stable Built-in bit46-only
  2. 2.5.2 Plugin Mode
```

默认保持模式 1。

## 3. 阶段 A：确认/修复 HW 选择

目标：先不恢复插件，只确认当前工程的 HW3/HW4/LEGACY 选择可用，且不影响稳定灰轮默认行为。

### A1. 代码检查

- [ ] 检查 `hwMode` 定义和默认值。
- [ ] 检查 `DASH_DEFAULT_HW=1` 是否仍为 HW3。
- [ ] 检查 `/config` 是否支持 `hw=0/1/2`。
- [ ] 检查 `dashSwapHandler(hwMode)` 是否会在切换后执行。
- [ ] 检查切换 HW 后是否重新设置 CAN filter。
- [ ] 检查 NVS `hw` 是否可能残留为 HW4/LEGACY。

### A2. WebUI 检查

- [ ] 检查 HW3/HW4/LEGACY 按钮是否显示。
- [ ] 检查按钮切换后 `/status.hw` 是否变化。
- [ ] 检查刷新页面后 HW 选择是否保持。
- [ ] 检查清除 NVS 后默认是否回到 HW3。

### A3. 构建验证

- [ ] 运行 `py -3 scripts\minify_dashboard.py`，如 UI 有改动。
- [ ] 运行 `pio run -e waveshare_ESP32_S3_RS485_CAN`。
- [ ] 保存 `firmware.bin` SHA256。

### A4. 实车/台架验证

- [ ] HW3 模式下 D 档灰轮稳定。
- [ ] 切 HW4 后状态显示正确，不误判为 HW3。
- [ ] 切 LEGACY 后状态显示正确，不误判为 HW3。
- [ ] 切回 HW3 后灰轮仍稳定。

预计时间：2-3 小时。  
建议推理程度：GPT-5.5 `high`。

## 4. 阶段 B：恢复最小插件执行链

目标：先不恢复完整 WebUI，只恢复最小后端插件能力，用于验证 2.5.2 插件模式是否能稳定复现。

### B1. 移植插件引擎核心

- [ ] 从 beta.5 优先移植 `include/plugin_engine.h`。
- [ ] 对照 2.5.2 校验插件规则语义。
- [ ] 保留以下最小 op：
  - [ ] `set_bit`
  - [ ] `set_byte`
  - [ ] `or_byte`
  - [ ] `and_byte`
  - [ ] `checksum`
- [ ] 暂缓复杂功能：
  - [ ] `emit_periodic`
  - [ ] `gtw_silent`
  - [ ] UDS keepalive
  - [ ] 插件优先级复杂 UI

### B2. 接入 CAN 时序

目标时序：

```text
read frame
  -> original = frame
  -> handler.handleMessage(frame)
  -> stable built-in 或 plugin mode 二选一
```

待实现：

- [ ] 增加 `FSD Activation Mode` 状态变量。
- [ ] 默认 `Stable Built-in bit46-only`。
- [ ] Plugin Mode 下执行 `pluginProcessFrame(original, driver)`。
- [ ] 防止内置 bit46 和插件 bit46 默认同时执行。
- [ ] 插件仍走 `apInjectionGate=false` 默认旁路语义。

### B3. 最小插件来源

先采用一种简单方式，不做完整 UI：

- [ ] 方案 1：内置测试插件 JSON。
- [ ] 方案 2：从 SPIFFS 固定路径加载 `/plugins/ad-activation-hw3.json`。
- [ ] 方案 3：临时 POST `/plugin_upload`，但不做完整列表 UI。

建议第一轮使用固定测试插件：

```json
{
  "name": "AD Activation HW3",
  "version": "1.0",
  "rules": [
    {
      "id": 1021,
      "mux": 0,
      "ops": [
        { "type": "set_bit", "bit": 46, "val": 1 }
      ]
    }
  ]
}
```

### B4. Filter 合并

- [ ] 恢复 `pluginGetFilterIds()`。
- [ ] 在 `dashSwapHandler()` / filter 设置处合并 handler filter 与 plugin filter。
- [ ] 对 1021 规则确认无影响，因为 HW3 静态 filter 已有 1021。
- [ ] 为未来非 1021 插件保留能力。

### B5. 状态输出

- [ ] `/status` 输出当前 activation mode。
- [ ] `/status` 输出插件数量。
- [ ] `/status` 输出插件是否启用。
- [ ] `/status` 输出最后一次插件注入时间或计数。

### B6. 验证

- [ ] 构建通过。
- [ ] 当前默认稳定模式灰轮不退化。
- [ ] Plugin Mode + 1021 mux0 bit46 插件灰轮稳定。
- [ ] Plugin Mode 下 TX 与 RX mux0 接近 1:1。
- [ ] 切回 Stable Built-in 后仍稳定。

预计时间：4-8 小时。  
建议推理程度：GPT-5.5 `xhigh`。

## 5. 阶段 C：恢复完整插件 WebUI 和 API

目标：恢复 2.5.2 / beta.5 的插件管理体验。

### C1. 后端 API

恢复以下接口：

- [ ] `GET /plugins`
- [ ] `POST /plugin_upload`
- [ ] `POST /plugin_install`
- [ ] `POST /plugin_toggle`
- [ ] `POST /plugin_remove`
- [ ] `POST /plugin_priority`
- [ ] `POST /plugin_test`
- [ ] `GET /plugin_test_status`
- [ ] `POST /plugin_test_stop`

### C2. SPIFFS 插件管理

- [ ] 插件保存路径。
- [ ] 插件文件名清理。
- [ ] 启动时 `pluginLoadAll()`。
- [ ] 删除插件文件。
- [ ] 插件排序和优先级。
- [ ] 插件数量限制。

### C3. WebUI 插件面板

从 beta.5 移植并合并：

- [ ] Plugins 卡片。
- [ ] 插件列表。
- [ ] 安装 URL。
- [ ] 上传 JSON。
- [ ] 启用/禁用。
- [ ] 删除。
- [ ] 优先级调整。
- [ ] Plugin Editor。
- [ ] 插件测试区。

注意：当前 WebUI 已有 DNS/STA-AP/AP-NAT 相关功能，合并时必须避免覆盖。

### C4. AP Injection Gate UI

- [ ] 恢复 AP Injection Gate 开关。
- [ ] 默认值必须为 `false`。
- [ ] UI 文案应说明：开启后会等 AP/Parked/Summoning，可能导致 Waiting AP。
- [ ] `/config apg=0/1`。
- [ ] NVS key `ap_gate`。

### C5. Replay Count UI

- [ ] 恢复插件 replay count。
- [ ] `/config plgr=`。
- [ ] NVS key `plg_rep`。
- [ ] 状态导出/导入。

### C6. 生成与构建

- [ ] 修改 `.src.h`。
- [ ] 执行 `py -3 scripts\minify_dashboard.py`。
- [ ] 确认 `.h` 生成成功。
- [ ] 构建 `pio run -e waveshare_ESP32_S3_RS485_CAN`。
- [ ] 记录固件大小和 SHA256。

预计时间：1-1.5 天。  
建议推理程度：GPT-5.5 `xhigh`。

## 6. 阶段 D：完整验证矩阵

### D1. 固件状态

- [ ] 清除 NVS 后默认 HW3。
- [ ] 清除 NVS 后 `apInjectionGate=false`。
- [ ] 清除 NVS 后默认 Stable Built-in Mode。
- [ ] WebUI 中文 `FSD 关闭` 正确。
- [ ] STA-AP 网关开关刷新后不丢。
- [ ] DNS 黑白名单保存正常。

### D2. CAN 稳定灰轮

- [ ] Stable Built-in Mode：D 档 3 秒内灰轮出现。
- [ ] Stable Built-in Mode：行驶 10-20 秒灰轮稳定。
- [ ] Stable Built-in Mode：重复 3 次一致。
- [ ] Turn FSD Off 后 TX 停止。

### D3. 插件模式

- [ ] 安装 1021 mux0 bit46 插件。
- [ ] 启用 Plugin Mode。
- [ ] D 档灰轮出现。
- [ ] 行驶 10-20 秒稳定。
- [ ] 关闭插件后灰轮消失或不再维持。
- [ ] 插件 TX 计数增长。
- [ ] 插件启用/禁用刷新后保持。

### D4. HW 选择

- [ ] HW3：稳定灰轮可用。
- [ ] HW4：handler 切换、状态显示和 filter 正常。
- [ ] LEGACY：handler 切换、状态显示和 filter 正常。
- [ ] 切回 HW3 后稳定灰轮不受影响。

### D5. 回归项

- [ ] OTA 上传功能可用。
- [ ] WebUI 刷新状态不闪回旧值。
- [ ] CAN RX 增长正常。
- [ ] TX errors 为 0 或可解释。
- [ ] 无异常重启。
- [ ] 日志无持续错误。

## 7. 备份与归档规则

每个阶段完成后都创建备份：

- [ ] `IDF稳定灰轮版-HW选择验证-YYYYMMDD-HHMMSS`
- [ ] `IDF稳定灰轮版-最小插件链-YYYYMMDD-HHMMSS`
- [ ] `IDF稳定灰轮版-插件WebUI-YYYYMMDD-HHMMSS`
- [ ] `IDF稳定灰轮版-插件完整验证-YYYYMMDD-HHMMSS`

每个备份包含：

- [ ] `BACKUP_INFO.txt`
- [ ] 当前 git/status 或文件哈希摘要。
- [ ] 固件路径。
- [ ] 固件 SHA256。
- [ ] 已验证项目。
- [ ] 已知风险。

## 8. 推荐执行顺序

推荐顺序：

```text
1. 备份当前 IDF稳定灰轮版
2. 阶段 A：确认 HW 选择
3. 阶段 B：最小插件执行链
4. 实车 A/B：Stable Built-in vs Plugin Mode
5. 阶段 C：完整插件 WebUI
6. 阶段 D：完整验证矩阵
7. 最终归档线刷包/OTA 包
```

不推荐：

```text
一次性把 beta.5 的插件 WebUI、后端、plugin_engine 全部覆盖进当前项目
```

原因：当前项目已经加入 STA-AP、DNS、AP-NAT、稳定灰轮修复，直接覆盖容易破坏已验证功能。

## 9. 时间与推理程度评估

| 工作项 | 推荐推理程度 | 预计时间 |
|---|---:|---:|
| 只检查 HW 选择 | high | 1-2 小时 |
| 修复/验证 HW 选择 | high | 2-3 小时 |
| 最小插件后端链路 | xhigh | 4-8 小时 |
| 完整插件后端 | xhigh | 6-10 小时 |
| 完整插件 WebUI | xhigh | 1-1.5 天 |
| 完整验证与归档 | high/xhigh | 0.5-1 天 |

整体估算：

```text
最小可测试版本：约 1 天
完整恢复版本：约 2-3 天
```

## 10. 下一步建议

建议下一步只做阶段 A：

```text
确认当前 HW3/HW4/LEGACY 选择是否完整可用，
不引入插件，
不改变当前稳定灰轮默认行为。
```

阶段 A 完成并备份后，再决定是否进入阶段 B。
