# 当前项目 vs ev-2.5.2 CAN 帧抓取/处理/修改/发送差异分析

生成时间：2026-05-14

当前项目：`C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT`

对比项目：`C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\ev-2.5.2`

重点对比 2.5.2 最终 FSD 激活插件规则：

```json
"rules": [
  {
    "id": 1021,
    "mux": 0,
    "ops": [
      { "type": "set_bit", "bit": 46, "val": 1 }
    ]
  }
]
```

---

## 1. 总结论

当前项目已经把 2.5.2 的 `1021 mux0 set_bit 46 = 1` 插件规则做成了内置固定逻辑。

2.5.2：

```text
真实车端 1021 mux0
  -> plugin rule 匹配
  -> copy original
  -> set bit46 = 1
  -> changed 才 send
```

当前项目：

```text
真实车端 1021 mux0
  -> dashPostProcessFrame 匹配
  -> copy original
  -> set bit46 = 1
  -> changed 才 send
```

因此，在以下条件满足时，两者最终发送的 `1021 mux0` payload 应该一致：

```text
HW3
canActive = true
AP Gate open
收到真实车端 1021 mux0
原始 bit46 = 0
```

最终发送结果都是：

```text
复制 original 原始帧
只设置 data[5] bit6，也就是 bit46 = 1
其他 byte 保持 original
不重算 checksum
只发送 1 帧
```

核心差异不是最终 payload，而是执行路径和门控条件：

```text
当前项目：内置固定 bit46-only，路径短，AP Gate 强制，CAN task 独立。
2.5.2：插件规则系统，路径更灵活，AP Gate 可配置，CAN/Web 在 Arduino loop 顺序跑。
```

---

## 2. 帧抓取入口对比

### 当前项目

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT\include\app.h
```

关键位置：

```text
include\app.h:241  while (appDriver->read(frame))
include\app.h:251  CanFrame original = frame;
include\app.h:253  h->handleMessage(frame, *appDriver);
include\app.h:255  dashPostProcessFrame(original, *appDriver);
```

当前主时序：

```text
read frame
  -> normalize bus
  -> save original
  -> handler.handleMessage(frame)
  -> dashPostProcessFrame(original)
```

### ev-2.5.2

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\ev-2.5.2\include\app.h
```

关键位置：

```text
ev-2.5.2\include\app.h:184  while (appDriver->read(frame))
ev-2.5.2\include\app.h:192  CanFrame original = frame;
ev-2.5.2\include\app.h:193  h->handleMessage(frame, *appDriver);
ev-2.5.2\include\app.h:194  if (appPluginProcess)
ev-2.5.2\include\app.h:195      appPluginProcess(original, *appDriver);
```

2.5.2 主时序：

```text
read frame
  -> normalize bus
  -> save original
  -> handler.handleMessage(frame)
  -> appPluginProcess(original)
```

### 结论

两者核心时序一致：

```text
read frame -> save original -> handler -> post process/plugin -> send
```

这是当前项目贴近 2.5.2 的关键。

---

## 3. 帧抓取调度模型差异

### 当前项目

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\IDF-DNS-AP-NAT\src\main.cpp
```

关键位置：

```text
src\main.cpp:160  static void app_can_task(void *)
src\main.cpp:165  bool processed = appLoop<TWAIDriver>();
src\main.cpp:182  xTaskCreatePinnedToCore(..., "can_rt", ...)
src\main.cpp:203  bool canTaskStarted = app_start_can_task();
src\main.cpp:213  mcpDashboardLoop();
```

当前模型：

```text
can_rt 独立任务：持续 appLoop<TWAIDriver>()
主任务：mcpDashboardLoop() / WebUI / OTA
```

影响：

```text
CAN 读取/处理和 WebUI 调度隔离更强。
高负载下更不容易被 WiFi/Web 卡住。
抓帧更稳，响应更确定。
```

### ev-2.5.2

文件：

```text
C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\ev-2.5.2\src\main.cpp
```

关键位置：

```text
ev-2.5.2\src\main.cpp:73  void loop()
ev-2.5.2\src\main.cpp:88  appLoop<TWAIDriver>();
ev-2.5.2\src\main.cpp:90  mcpDashboardLoop();
```

2.5.2 模型：

```text
Arduino loop():
  appLoop<TWAIDriver>()
  mcpDashboardLoop()
```

影响：

```text
CAN 和 WebUI 在同一个 loop 顺序跑。
Web/OTA/插件负载高时，CAN 处理抖动可能更大。
```

---

## 4. TWAI 队列和底层读写差异

### 当前项目

配置：

```text
platformio.ini:164  -DTWAI_RX_QUEUE_LEN=64
platformio.ini:165  -DTWAI_TX_QUEUE_LEN=16
```

驱动位置：

```text
include\drivers\twai_driver.h:35  g_config_.rx_queue_len = TWAI_RX_QUEUE_LEN;
include\drivers\twai_driver.h:36  g_config_.tx_queue_len = TWAI_TX_QUEUE_LEN;
include\drivers\twai_driver.h:90  twai_receive(&msg, 0)
include\drivers\twai_driver.h:133 twai_transmit(&msg, pdMS_TO_TICKS(2))
```

### ev-2.5.2

驱动位置：

```text
ev-2.5.2\include\drivers\twai_driver.h:25  g_config_.rx_queue_len = 32;
ev-2.5.2\include\drivers\twai_driver.h:26  g_config_.tx_queue_len = 16;
ev-2.5.2\include\drivers\twai_driver.h:80  twai_receive(&msg, 0)
ev-2.5.2\include\drivers\twai_driver.h:123 twai_transmit(&msg, pdMS_TO_TICKS(2))
```

### 结论

```text
当前项目 RX queue = 64，2.5.2 RX queue = 32。
TX queue 都是 16。
读都是 non-blocking twai_receive(..., 0)。
发送都是 2ms timeout。
```

影响：

```text
当前项目更抗瞬时 RX 堆积。
底层发送超时策略基本一致。
```

---

## 5. CAN 过滤差异

### HW3 基础过滤

当前项目 HW3：

```text
include\handlers.h:311  const uint32_t *filterIds() const override
include\handlers.h:313  {280, 390, 921, 1016, 1021, 2047}
```

2.5.2 HW3：

```text
ev-2.5.2\include\handlers.h:249  const uint32_t *filterIds() const override
ev-2.5.2\include\handlers.h:251  {280, 390, 921, 1016, 1021, 2047}
```

对 `1021 mux0` 来说，基础过滤一致。

### 2.5.2 插件 filter 合并

2.5.2 额外支持插件动态添加 filter：

```text
ev-2.5.2\include\web\mcp2515_dashboard.h:1315  dashReapplyFiltersWithPlugins()
ev-2.5.2\include\web\mcp2515_dashboard.h:1326  pluginGetFilterIds(...)
ev-2.5.2\include\web\mcp2515_dashboard.h:1341  dashDriver->setFilters(mergedIds, count)
```

当前项目没有插件系统，所以没有插件 filter 合并。

结论：

```text
对于 1021 mux0：无本质差异，因为 HW3 handler 已经抓 1021。
对于自定义其他 CAN ID：2.5.2 可通过插件扩展，当前项目不可扩展。
```

---

## 6. handler 处理差异

### 两者都会先更新车辆状态

两者 HW3 handler 都会处理：

```text
280  -> Parked / Summoning
390  -> Parked
1016 -> Summoning / follow distance / speed profile
921  -> APActive
2047 -> gatewayAutopilot
1021 mux0 -> ADEnabled / speedOffset
```

当前项目 HW3 handler：

```text
include\handlers.h:318  void handleMessage(CanFrame &frame, CanDriver &driver) override
```

2.5.2 HW3 handler：

```text
ev-2.5.2\include\handlers.h:256  void handleMessage(CanFrame &frame, CanDriver &driver) override
```

### 关键差异：handler 内是否直接发送 1021 mux0

当前项目在 `DASH_FSD_252_COMPAT=1` 下，handler 内不直接发送：

```text
include\handlers.h:433  #if defined(ESP32_DASHBOARD) && DASH_FSD_252_COMPAT
include\handlers.h:434  // 2.5.2 compat sends after the handler from the saved original frame
```

如果不是 compat，才会：

```text
include\handlers.h:438  setSpeedProfileV12V13(frame, speedProfile);
include\handlers.h:439  setBit(frame, 46, true);
include\handlers.h:441  driver.send(frame);
```

2.5.2 Dashboard 下，如果 `shouldInjectSpeedProfile()` 为 true，handler 可能先发一帧：

```text
ev-2.5.2\include\handlers.h:360  if (shouldInjectSpeedProfile())
ev-2.5.2\include\handlers.h:362  setSpeedProfileV12V13(frame, speedProfile);
ev-2.5.2\include\handlers.h:363  setBit(frame, 46, true);
ev-2.5.2\include\handlers.h:365  driver.send(frame);
```

结论：

```text
当前项目 compat 模式：handler 不直接发 1021 mux0。
2.5.2 默认 speedProfileAuto=true 时通常也不发。
2.5.2 若手动 speed profile/custom speed 生效，handler 可能先发额外一帧。
```

---

## 7. AP Gate 差异

### 当前项目

```text
include\web\mcp2515_dashboard.h:603  dashApInjectionAllowed()
include\web\mcp2515_dashboard.h:605  return dashHandler && dashHandler->injectionGateOpen();
include\web\mcp2515_dashboard.h:608  dashInjectionActive()
include\web\mcp2515_dashboard.h:610  return canActive && dashApInjectionAllowed();
```

当前项目等价于：

```text
canActive && injectionGateOpen()
```

AP Gate 是强制的。

### ev-2.5.2

```text
ev-2.5.2\include\web\mcp2515_dashboard.h:412  dashApInjectionAllowed()
ev-2.5.2\include\web\mcp2515_dashboard.h:414  return !apInjectionGate || (dashHandler && dashHandler->injectionGateOpen());
ev-2.5.2\include\web\mcp2515_dashboard.h:417  dashInjectionActive()
ev-2.5.2\include\web\mcp2515_dashboard.h:419  return canActive && dashApInjectionAllowed();
```

2.5.2 等价于：

```text
canActive && (!apInjectionGate || injectionGateOpen())
```

AP Gate 可配置开/关。

### 影响

```text
当前项目：
  AP Gate 不开就不会发 1021 mux0 bit46。
  会显示 Waiting AP，TX=0。

2.5.2：
  如果 AP Gate 打开，行为接近当前项目。
  如果 AP Gate 关闭，只要 canActive=true，插件就能发。
```

这是当前项目和 2.5.2 最大的“是否允许发送”差异。

---

## 8. 2.5.2 插件规则解析和执行

你给的规则：

```json
{
  "id": 1021,
  "mux": 0,
  "ops": [
    { "type": "set_bit", "bit": 46, "val": 1 }
  ]
}
```

在 2.5.2 会解析为：

```text
canId = 1021
mux = 0
muxMask = 0x07
busMask = CAN_BUS_ANY
sendAfter = true
op = OP_SET_BIT
op.index = 46
op.value = 1
```

解析位置：

```text
ev-2.5.2\include\plugin_engine.h:540  JsonArray rules = doc["rules"]
ev-2.5.2\include\plugin_engine.h:549  r.canId = rule["id"]
ev-2.5.2\include\plugin_engine.h:550  int mux = rule["mux"]
ev-2.5.2\include\plugin_engine.h:559  r.muxMask = ... pluginDefaultMuxMask(r.mux)
ev-2.5.2\include\plugin_engine.h:562  r.busMask = pluginParseBus(rule["bus"])
ev-2.5.2\include\plugin_engine.h:563  r.sendAfter = rule["send"] | true
ev-2.5.2\include\plugin_engine.h:580  if (strcmp(type, "set_bit") == 0)
ev-2.5.2\include\plugin_engine.h:583  o.index = op["bit"]
ev-2.5.2\include\plugin_engine.h:584  o.value = op["val"]
```

mux 匹配：

```text
ev-2.5.2\include\plugin_engine.h:505  pluginRuleMatchesMux(...)
ev-2.5.2\include\plugin_engine.h:511  uint8_t mask = ...
ev-2.5.2\include\plugin_engine.h:512  return (frame.data[0] & mask) == (rule.mux & mask)
```

所以 `mux: 0` 实际就是：

```text
(frame.data[0] & 0x07) == 0
```

这和当前项目 `readMuxID(original) == 0` 一致。

---

## 9. 2.5.2 插件修改/发送链路

2.5.2 插件执行核心：

```text
ev-2.5.2\include\plugin_engine.h:1092  pluginProcessFrame(const CanFrame &original, CanDriver &driver)
ev-2.5.2\include\plugin_engine.h:1118  CanFrame modified = original;
ev-2.5.2\include\plugin_engine.h:1128  if (rule.canId != original.id) continue;
ev-2.5.2\include\plugin_engine.h:1131  pluginRuleMatchesBus / pluginRuleMatchesMux
ev-2.5.2\include\plugin_engine.h:1172  pluginApplyOpMasked(modified, op, allowedMask)
ev-2.5.2\include\plugin_engine.h:1187  if (pluginFrameChanged(original, modified))
ev-2.5.2\include\plugin_engine.h:1193  driver.send(replayFrame)
```

`set_bit` 实际执行：

```text
ev-2.5.2\include\plugin_engine.h:920  case OP_SET_BIT
ev-2.5.2\include\plugin_engine.h:923  setBit(frame, op.index, op.value)
```

`bit46` 对应：

```text
byte index = 46 / 8 = 5
bit index  = 46 % 8 = 6
即 data[5] bit6
```

---

## 10. 当前项目内置 bit46 修改/发送链路

当前项目固定逻辑：

```text
include\web\mcp2515_dashboard.h:613  dashPostProcessFrame(const CanFrame &original, CanDriver &driver)
include\web\mcp2515_dashboard.h:616  if (hwMode != 1 || !dashInjectionActive()) return;
include\web\mcp2515_dashboard.h:618  if (original.id != 1021 || original.dlc < 8 || readMuxID(original) != 0) return;
include\web\mcp2515_dashboard.h:621  CanFrame modified = original;
include\web\mcp2515_dashboard.h:622  setBit(modified, 46, true);
include\web\mcp2515_dashboard.h:623  if (!framePayloadChanged(original, modified)) return;
include\web\mcp2515_dashboard.h:628  driver.send(modified);
```

当前项目相当于硬编码了：

```json
{
  "id": 1021,
  "mux": 0,
  "ops": [
    { "type": "set_bit", "bit": 46, "val": 1 }
  ]
}
```

但没有插件 enabled、priority、rule traversal、bus 配置、sendAfter 配置等逻辑。

---

## 11. changed 才发送：两者一致

当前项目：

```text
include\web\mcp2515_dashboard.h:623  if (!framePayloadChanged(original, modified)) return;
include\handlers.h:27              framePayloadChanged(...)
```

2.5.2：

```text
ev-2.5.2\include\plugin_engine.h:1187  if (pluginFrameChanged(original, modified))
ev-2.5.2\include\plugin_engine.h:996   pluginFrameChanged(...)
```

结论：

```text
如果原始 1021 mux0 的 bit46 已经是 1：
  当前项目不发送。
  2.5.2 插件也不发送。
```

---

## 12. mux1 / mux2 对比

当前项目 `DASH_FSD_252_COMPAT=1`：

```text
mux0：post-handler original-frame 设置 bit46 并发送
mux1：不发送
mux2：不发送
```

位置：

```text
include\handlers.h:433  mux0 compat 不在 handler 内发
include\handlers.h:448  mux1 compat 不发送
include\handlers.h:480  mux2 只在 !DASH_FSD_252_COMPAT 下编译
```

2.5.2 你给的插件规则只匹配：

```text
id = 1021
mux = 0
```

所以：

```text
mux0：匹配并发送
mux1：不匹配，不发送
mux2：不匹配，不发送
```

结论：

```text
只看最终 FSD 激活规则时，两者都是 mux0-only。
mux1/mux2 不参与最终 bit46 激活发送。
```

---

## 13. 发送次数对比

当前项目：

```text
每收到一个真实 1021 mux0：
  AP Gate 打开
  bit46 原来是 0
  最多发送 1 帧
```

2.5.2 对 1021：

```text
每收到一个真实 1021 mux0：
  canActive / AP Gate / plugin enabled 允许
  bit46 原来是 0
  最多发送 1 帧
```

2.5.2 replay 只对 2047 特殊：

```text
ev-2.5.2\include\plugin_engine.h:1189
uint8_t replayCount = original.id == 2047 ? pluginGetReplayCount() : 1;
```

结论：

```text
1021 mux0 bit46 这条路径，两者都不会多次 replay。
```

---

## 14. 2.5.2 插件路径的门控

2.5.2 插件要真正发出 `1021 mux0 bit46`，需要通过：

```text
canActive = true
AP Gate 允许，或者 AP Gate 配置关闭
appPluginProcess 已挂接
pluginCount > 0
pluginsLocked == false
pluginStore[p].enabled = true
rule.canId == 1021
rule.bus 匹配
rule.mux == 0 匹配
rule.sendAfter == true
op 没被更高优先级插件 claimed
modified != original
```

当前项目只需要：

```text
hwMode == HW3
canActive = true
AP Gate open
original.id == 1021
original.dlc >= 8
readMuxID(original) == 0
modified != original
```

影响：

```text
当前项目：
  路径短，不依赖 SPIFFS 插件文件。
  不会因为插件没加载/没启用/优先级冲突导致不发。
  但不可动态改规则。

2.5.2：
  灵活，可扩展。
  但插件状态、SPIFFS、规则优先级都会影响最终是否发送。
```

---

## 15. 当前项目比 2.5.2 多出的限制

当前项目多了这些限制：

```text
1. 强制 HW3：hwMode != 1 直接 return。
2. 强制 AP Gate：不能像 2.5.2 那样通过 apInjectionGate=false 跳过 gate。
3. 固定只做 bit46：不能通过插件改规则。
4. 固定 dlc >= 8：2.5.2 插件没有显式要求 dlc >= 8，但真实 1021 正常是 8 字节。
```

最关键差异：

```text
当前项目如果 Waiting AP，就 TX=0。
2.5.2 如果关闭 AP Gate，可以不等 AP 直接注入。
```

---

## 16. forceActivate / Turn FSD On 差异

当前项目有 FSD Master Switch：

```text
include\web\mcp2515_dashboard.h:675  forceActivateRuntime = canActive && forceActivate;
```

当前 HW3 handler 更新 ADEnabled 时：

```text
include\handlers.h:423  const bool fsdRequested = forceActivateRuntime || isADSelectedInUI(frame);
include\handlers.h:424  ADEnabled = fsdRequested && (!checkAD || checkAD());
```

2.5.2 handler：

```text
ev-2.5.2\include\handlers.h:354  if (index == 0)
ev-2.5.2\include\handlers.h:355      ADEnabled = isADSelectedInUI(frame) && (!checkAD || checkAD());
```

但注意：

```text
当前 post-handler 注入不依赖 ADEnabled。
2.5.2 plugin 注入也不依赖 ADEnabled。
```

所以这个差异主要影响 WebUI 状态和 handler 内部状态，不是最终 bit46 插件发送条件。

---

## 17. 最终激活帧内容对比

假设收到真实车端帧：

```text
CAN ID = 1021
mux = 0
dlc = 8
data[5] bit6 = 0
```

2.5.2 插件输出：

```text
复制 original
data[5] bit6 = 1
其他 byte 保持 original
不重算 checksum
发送 1 帧
```

当前项目输出：

```text
复制 original
data[5] bit6 = 1
其他 byte 保持 original
不重算 checksum
发送 1 帧
```

结论：

```text
在 AP Gate 打开、HW3、bit46 原来为 0 的前提下：
当前项目和 2.5.2 这条插件规则发出的 1021 mux0 payload 应该一致。
```

---

## 18. 时效性对比

2.5.2 路径：

```text
read
  -> handler
  -> dashPluginProcess
  -> dashInjectionActive
  -> pluginProcessFrame
  -> 遍历 plugin
  -> 遍历 rule
  -> 匹配 id/bus/mux
  -> apply op
  -> pluginFrameChanged
  -> send
```

当前项目路径：

```text
read
  -> handler
  -> dashPostProcessFrame
  -> dashInjectionActive
  -> 匹配 id/mux
  -> set bit46
  -> framePayloadChanged
  -> send
```

影响：

```text
当前项目少了插件遍历、规则遍历、op claim、priority、beforeSend 等步骤。
当前项目在同一帧内理论上更快一点。
但差异很小，通常仍在同一个 CAN loop 周期内完成。
```

实车上更明显的差异通常是：

```text
1. AP Gate 是否打开
2. CAN task 是否被 Web/WiFi 影响
3. 是否 handler 先发了额外帧
4. 是否插件加载/启用成功
```

---

## 19. 最终差异清单

### 相同点

```text
1. 都先保存 original。
2. 都先跑 handler，再注入。
3. 都基于 original 修改，不基于 handler 修改后的 frame。
4. 都匹配 1021 mux0。
5. 都只设置 bit46 = 1。
6. 都不重算 checksum。
7. 都是 changed 才 send。
8. 对 1021 都只发 1 帧，不 replay。
9. mux1/mux2 都不参与这条插件规则。
10. 底层 TWAI 发送都是 2ms timeout。
```

### 不同点

```text
1. 当前项目是内置固定逻辑；2.5.2 是插件规则逻辑。
2. 当前项目 AP Gate 强制；2.5.2 AP Gate 可关闭。
3. 当前项目不依赖插件加载/启用；2.5.2 必须插件存在并启用。
4. 当前项目只在 HW3 执行；2.5.2 插件引擎本身不强制 HW3。
5. 当前项目 CAN task 独立；2.5.2 CAN/Web 在 Arduino loop 顺序跑。
6. 当前项目 RX queue 64；2.5.2 RX queue 32。
7. 2.5.2 如果手动 speed profile 开启，handler 可能先发额外 1021 mux0；当前 compat 模式不会。
8. 2.5.2 有插件优先级、claim、bus/mux rule、checksum/counter/periodic 机制；当前项目没有。
```

---

## 20. 建议

如果目标是复刻 2.5.2 的这条最终 FSD 激活规则：

```json
id 1021
mux 0
set_bit 46 = 1
```

当前项目已经足够接近，而且更确定：

```text
post-handler original-frame + AP Gate + bit46-only
```

不建议为了“完全像 2.5.2 插件引擎”再引入完整插件复杂度，因为当前项目的固定路径更短、更少状态依赖，更适合实车稳定测试。

如果后续要进一步贴近 2.5.2 行为，可以考虑：

```text
1. 增加 AP Gate 可开/关配置，模拟 2.5.2 的 apInjectionGate。
2. 保持当前 bit46-only 内置逻辑，不恢复 mux1/mux2。
3. 保留 can_rt 独立任务，不为了复刻 Arduino loop 而降低 CAN 调度稳定性。
```
