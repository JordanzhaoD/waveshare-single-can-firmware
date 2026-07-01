# Human Torque Replay v3 — Legacy NAG 抑制设计

- **日期**: 2026-06-26
- **状态**: Spec（Jordan 已批准方案 A，待 Jordan review 本文）
- **方案**: A — Human Torque Replay v3
- **取代/推进**: `2026-06-26-reactive-nag-v2-proactive-sustained-design.md` 的短 burst DC 策略
- **范围**: Waveshare 单 CAN LegacyHandler；仅 0x399 识别 + 0x370 replay 注入；不修改 0x399

---

## 1. 背景

v2 实车结果：

- ✅ NAG 抑制开关开启，FSD 正常，无报错。
- ✅ 板上持久化诊断显示链路完整运行：`enabled=1 nagSamples=449 reactiveBursts=119 proactiveWiggles=359 echoSent=10868`，NVS 同值。
- ✅ 10 个实车 CSV 中，`can_recording(5).csv` 与 `can_recording(7).csv` 均有约 6.9s 连续 `0x399 byte5 bits[5:2]=3` NAG 窗口；窗口内 0x370 TX 分别 75/86 帧。
- ✅ TX 帧按 v2 正确改帧：`data[2:3]` 增量约 `+78..+92` raw（约 +0.78..+0.92Nm）、`data[4] 0x20→0x60`、counter +1、checksum 合法。
- ❌ NAG 仍未清除，后续 0x399 HOS 仍保持 3。

结论：v2 的工程链路没有问题；失败点是 **v2 的扭矩时间签名不像真实人工握盘/转盘，DAS 不把它当有效 hands-on**。

Jordan 提供新资料：

`/Users/ziwind/Codex/DouyinFSD /抓包握方向盘警告后扭矩数据 2.txt`

该文档包含三段「握盘警告 → 人工转动方向盘 → 警告被手动消除 → 继续追踪」的 0x370 真实物理扭矩串口日志。

---

## 2. 新抓包证据：真实人工清除 NAG 的扭矩签名

### 2.1 事件 1：负向人工干预清除

警告消除前出现连续强负向扭矩：

```text
-1.00 → -1.23 → -1.54 → -1.73 → -1.76 → -1.62 → -1.28 → -0.68 Nm
```

统计：

```text
|torque| >= 1.0Nm 连续约 13 帧
|torque| >= 1.5Nm 连续约 10 帧
```

### 2.2 事件 2：正向人工干预清除

警告消除前出现连续强正向扭矩：

```text
+1.03 → +1.20 → +1.36 → +1.50 → +1.62 → +1.68 → +1.58 → +1.36 → +1.07 Nm
```

统计：

```text
|torque| >= 1.0Nm 连续约 12 帧
|torque| >= 1.2Nm 连续约 10 帧
|torque| >= 1.5Nm 连续约 4 帧
```

### 2.3 事件 3：正向人工干预清除

警告消除前出现连续强正向扭矩：

```text
+1.11 → +1.66 → +1.74 → +1.76 → +1.56 → +1.39 → +1.16 Nm
```

统计：

```text
|torque| >= 1.0Nm 连续约 10 帧
|torque| >= 1.5Nm 连续约 7 帧
```

### 2.4 关键模式

真实人工清除 NAG 的共同点：

| 维度 | 真实人工数据 |
|---|---|
| 幅度 | 常见峰值 ±1.5..±1.8Nm |
| 持续 | 清除前连续 10..13 帧强扭矩 |
| 方向 | 可正可负，但单次清除通常持续同方向 |
| 形态 | ramp up → 峰值 hold → ramp down |
| `data[4]` | 大部分仍是 handsBits=0，只有清除临界附近偶发 handsBits=1 |

因此，真实有效信号主要是 **0x370 物理扭矩字段的强连续轨迹**，不是 `data[4]`。

---

## 3. v2 与真实人工的差异

| 维度 | v2 实车 TX | 真实人工清除 |
|---|---|---|
| 扭矩幅度 | +0.78..+0.92Nm | 常见 ±1.0..±1.8Nm |
| 方向 | 固定正向 DC | 正向或负向，单次持续同方向 |
| 持续 | 0.7..1.2s burst 后有 0.9..1.2s 空窗 | 清除前连续 10..13 帧强扭矩 |
| 形态 | DC + 小 jitter | ramp → hold → decay |
| 反馈 | 不看 HOS 是否下降就继续固定节奏 | 人手实际会持续施力直到 NAG 清除 |

v2 失败的最强解释：**扭矩 profile 的幅度、方向和持续形态都不像真实人工干预**。

---

## 4. 目标

v3 目标是把真实人工清除 NAG 的 0x370 扭矩轨迹压缩成有限、安全、可诊断的 replay profile：

```text
检测 0x399 HOS >= 3
  → 选择一个 human torque profile
  → 连续 10..14 帧输出近似真实人工扭矩轨迹
  → 观察后续 0x399 HOS 是否降到 <=2
  → 成功即停止；失败则有限次数反向重试
```

成功标准：

1. 实车 NAG 窗口中，0x399 HOS 从 `>=3` 降到 `<=2`。
2. FSD 正常运行，无 EPAS/DAS/车身控制报错。
3. 串口/状态页能解释每次尝试：profile、方向、峰值、HOS before/after、success/fail。

非目标：

- 不伪造 0x399。
- 不新增自动开机启用。
- 不一次性合并非 0x370 路线（如 0x3C2 volume/speed spoof）。
- 不做无限重试或无界增幅。

---

## 5. 设计选型

### 5.1 方案 A — Human Torque Replay v3（已批准，推荐）

使用真实人工干预数据生成 profile：

- 正向中等
- 正向强
- 负向中等
- 负向强

每次 NAG 触发时输出一个 10..14 帧的连续 profile。若 HOS 未清除，下一组换方向重试。最多 3 组，之后冷却。

优点：最贴近实车有效数据，信息增益最高。  
缺点：比 v2 多一个 profile 状态机，必须严格诊断和限幅。

### 5.2 方案 B — v2 调参

只把 v2 的幅度提高、允许负向、延长 burst。

优点：改动小。  
缺点：仍是猜参数，不如直接模拟真实人工数据。

### 5.3 方案 C — 放弃 0x370，转非 EPAS DND

改走 0x3C2 volume/speed 等公开源码其他 DND 路线。

优点：避开扭矩路线。  
缺点：新抓包证明真实清除确实表现为 0x370 扭矩轨迹，现在直接放弃为时过早。

**决策**：Jordan 已批准方案 A。

---

## 6. v3 架构

```text
LegacyHandler
├─ 0x399 handler
│  └─ 解码 HOS = (data[5] >> 2) & 0x0F
│     └─ humanReplay.onHosSample(hos, nowMs, active)
│
├─ 0x370 handler
│  └─ 如果 humanReplay.shouldEcho(nowMs)
│     ├─ 从 RX 0x370 data[2:3] 解码当前真实 torque12
│     ├─ 应用当前 replay profile 的 deltaRaw
│     ├─ 写回 data[2:3]
│     ├─ data[4] 可选设 handsOn=1（辅助，不作为主信号）
│     ├─ counter +1
│     ├─ checksum = sum(data[0..6]) + 0x73
│     └─ driver.send(echo)
│
└─ /status + serial reactive_nag
   └─ 输出 replay attempts/success/profile/peak/HOS before-after
```

新状态机建议命名：

```cpp
DashHumanTorqueReplayNag
```

也可以在现有 `DashReactiveNagBurst` 上演进，但建议保留清晰命名，避免 v1/v2 语义混乱。

---

## 7. 数据模型

### 7.1 raw 与 Nm 的换算

从文档解析看，0x370 Legacy torque12 的 raw signed 约等于：

```text
1 raw ≈ 0.01 Nm
```

例：

```text
raw +170 ≈ +1.68Nm
raw -174 ≈ -1.76Nm
```

因此 profile 可以以 raw delta 表示，避免浮点和单位混乱。

### 7.2 profile 定义

设计级 profile（raw 单位，初值可微调）：

```cpp
// 中等正向：峰值约 +1.45Nm
P_POS_MED = { +40, +80, +110, +130, +145, +145, +130, +105, +75, +40 };

// 强正向：峰值约 +1.75Nm
P_POS_STRONG = { +50, +100, +140, +165, +175, +170, +150, +120, +80, +45 };

// 中等负向：峰值约 -1.45Nm
P_NEG_MED = { -40, -80, -110, -130, -145, -145, -130, -105, -75, -40 };

// 强负向：峰值约 -1.75Nm
P_NEG_STRONG = { -50, -100, -140, -165, -175, -170, -150, -120, -80, -45 };
```

原则：

- 峰值对齐真实人工 `±1.5..±1.8Nm`。
- 持续 10 帧左右，对齐真实清除前的强扭矩 run。
- 不超过 `±180 raw` 初始硬上限。
- 后续若要调整，必须基于实车 CSV 与诊断，而不是盲目加大。

### 7.3 帧步进

每次 0x370 RX 来一帧，profile index +1。

好处：

- 与车端 0x370 实际频率同步。
- 避免 `millis()` 抖动导致漏步。
- 真实人工数据也是连续 0x370 帧序列。

注意：v2 曾把 burst 改为帧驱动导致 40..60ms 太短；v3 不同，因为 profile 本身明确有 10..14 帧，按 0x370 约 25Hz 计算约 400..560ms，正好匹配真实人工强扭矩 run。

---

## 8. 状态机

### 8.1 状态

```text
IDLE
  无 NAG 或未启用

ARMED
  检测到 HOS>=3，准备发 replay profile

REPLAYING
  正在逐 0x370 帧输出 profile

OBSERVING
  profile 打完，等待 1..2 个 0x399 样本观察 HOS 是否下降

COOLDOWN
  达到最大尝试次数或安全节流
```

### 8.2 触发

```text
onHosSample(hos):
  if hos <= 2:
      success if previous state was REPLAYING/OBSERVING
      reset to IDLE

  if hos >= 3 and active:
      if IDLE:
          start attempt #1
      if OBSERVING and enough samples observed and hos still >=3:
          start next attempt with opposite direction / stronger profile
```

### 8.3 尝试策略

建议初版：

```text
attempt 1: 根据最近原始 0x370 torque 方向选择中等 profile
attempt 2: 反方向中等 profile
attempt 3: 选择绝对值更强的一侧 strong profile
then cooldown 3s
```

如果最近原始 torque 接近 0：

```text
attempt 1 使用上次成功方向；若无历史，正/负交替
```

### 8.4 成功判定

```text
任意时刻 HOS <= 2：
  clearSuccess++
  停止 replay
  清 attempt 状态
  进入 IDLE/轻冷却
```

### 8.5 失败判定

```text
profile 打完后观察 1..2 个 0x399 样本：
  如果 HOS 仍 >=3：认为本 attempt 未清除
  如果 attempts < 3：下一 attempt
  否则进入 3s cooldown
```

---

## 9. 0x370 帧构造

输入：当前 RX 0x370。

```cpp
baseRaw = ((frame.data[2] & 0x0F) << 8) | frame.data[3];
signedBase = signExtend12(baseRaw);          // 0x800 附近为 0Nm
signedOut = signedBase + profileDeltaRaw;
signedOut = clamp(signedOut, -220, +220);    // 初版绝对安全上限，可按测试调整
outRaw = encodeSigned12(signedOut);
```

写回：

```cpp
echo.data[0] = frame.data[0];
echo.data[1] = frame.data[1];
echo.data[2] = (frame.data[2] & 0xF0) | ((outRaw >> 8) & 0x0F);
echo.data[3] = outRaw & 0xFF;
```

`data[4]`：

```cpp
// 初版建议仍设置 handsOn=1 作为辅助，虽然真实数据显示它不是主信号。
echo.data[4] = (frame.data[4] & 0x3F) | 0x40;
```

理由：

- v2 已证明 `data[4]=0x60` 不报错。
- 真实人工清除临界附近偶发 handsBits=1。
- 它不是主信号，但保留为辅助信号风险可控。

counter/checksum：沿用 v2，已被实车 CSV 证明能正确形成帧。

```cpp
counter = (frame.data[6] + 1) & 0x0F;
checksum = (sum(data[0..6]) + 0x73) & 0xFF;
```

---

## 10. 门控与安全边界

必须保留：

```text
☑ 默认 OFF
☑ 仅 LegacyHandler
☑ bionic/NAG 抑制开关 ON
☑ canActive=true
☑ APActive=true
☑ checkAD=true
☑ 0x399 HOS>=3 才进入 replay
☑ HOS<=2 立即停止
☑ toggle OFF 立即停止
☑ APActive/canActive 丢失立即停止
☑ 最多 3 次 attempt，之后 3s cooldown
☑ 单帧 deltaRaw 初版硬限 ±180
☑ signedOut 初版硬限 ±220 raw（约 ±2.2Nm）
```

为什么允许到 ±1.8Nm：

- 真实人工清除日志已出现 `±1.7..±1.98Nm`。
- v2 `+0.9Nm` 不足以清除。
- 初版不超过真实人工观测上界附近，且有 attempt/cooldown 限制。

---

## 11. 诊断设计

`reactive_nag` 串口与 `/status.reactiveNag` 应新增：

```json
{
  "enabled": true,
  "mode": "IDLE|ARMED|REPLAYING|OBSERVING|COOLDOWN",
  "lastHos": 3,
  "attempts": 2,
  "successes": 1,
  "failures": 1,
  "lastProfileId": "NEG_STRONG",
  "lastProfileDir": -1,
  "lastPeakRaw": -175,
  "lastBaseRaw": 12,
  "lastOutRaw": -163,
  "profileIndex": 7,
  "echoSent": 1234,
  "replayEchoSent": 42,
  "lastHosBefore": 3,
  "lastHosAfter": 2,
  "cooldownRemainMs": 0,
  "blockedReason": "none|toggle|ap|can|checkAD|cooldown|maxAttempts"
}
```

最关键诊断：

- 打了几个 attempt？
- 用了正向还是负向？
- 峰值多少？
- HOS before/after 是否下降？
- 若没打，blockedReason 是什么？

---

## 12. 测试策略

### 12.1 Native unit tests

新增/更新 `test_native_reactive_nag`：

1. HOS>=3 后进入 ARMED/REPLAYING。
2. profile 按 0x370 帧步进输出完整 delta 序列。
3. attempt 1 失败后 attempt 2 反向。
4. HOS<=2 立即成功并停止。
5. 超过 3 attempts 进入 cooldown。
6. toggle/checkAD/APActive/canActive false 时不 replay。
7. deltaRaw 与 signedOut clamp 生效。
8. diagnostic counters 正确累计。

### 12.2 Legacy integration tests

在 `test_native_legacy` 中验证：

1. 0x399 HOS=3 + active → 0x370 TX。
2. TX torque 写入 data[2:3]，允许正负 delta。
3. `data[4]` 设置为 0x40 辅助 handsOn。
4. counter +1，checksum 正确。
5. HOS<=2 后后续 0x370 不 TX。
6. 失败重试方向切换。
7. max attempts 后 cooldown 阻断。

### 12.3 Contract tests

Python 契约：

- 默认 OFF。
- 存在硬限 `kMaxAttempts=3`、`kCooldownMs`、`kMaxDeltaRaw<=180`。
- 不修改/伪造 0x399。
- `reactiveNag` 诊断字段包含 profile/attempt/HOS before-after。

### 12.4 Hardware/bench validation

烧板前：

- clean build。
- 资产 SHA256。
- 串口 smoke。
- 使用旧 CSV 或合成帧做台架回放：确认 HOS=3 后输出 profile，而不是 v2 的短 DC burst。

---

## 13. 实车验证流程

1. 上车前确认：

```text
canActive=NO 或开关 OFF
reactive_nag 显示 enabled=0 或 mode=IDLE
```

2. 进入安全区，手扶方向盘。
3. 打开 FSD，确认无异常。
4. 打开 NAG 抑制开关。
5. 触发 NAG。
6. 观察：

```text
是否在 1~2 个 attempt 内 HOS 从 >=3 降到 <=2
是否有报错
是否出现方向盘异常体感
```

7. 测后立刻读取：

```text
reactive_nag
```

8. 提供 CSV，分析：

- NAG 窗口起止。
- replay TX 是否连续 10..14 帧。
- 峰值是否符合 profile。
- HOS 是否下降。
- 哪个 profile 成功/失败。

### 成功判定

```text
HOS>=3 后，profile replay 期间或 replay 后 1s 内降为 <=2；FSD/车辆无报错。
```

### 失败判定

```text
连续 3 attempts 后 HOS 仍 >=3，且无报错。
```

失败时不继续盲目加幅度；转向：

- 分析真实人工 profile 与 replay 差异。
- 或停止 0x370 路线，评估 0x3C2/volume/speed DND。

---

## 14. 实施边界

本 spec 只批准设计，不代表现在开始写代码。实施前需要 implementation plan。

不做：

- 不 push。
- 不 release。
- 不默认启用。
- 不增加超过真实人工观测范围的扭矩。
- 不引入非 0x370 DND 路线。

做：

- 用真实人工数据生成 deterministic replay profile。
- 用诊断证明每次 attempt 是否有效。
- 用最小实车循环验证是否能清 NAG。

---

## 15. 自审结果

- 无未完成占位符。
- 与 v2 失败证据一致：链路有效，策略无效。
- 与新抓包一致：真实人工清除表现为 ±1.0..±1.8Nm 连续强扭矩。
- 范围聚焦：仅 Legacy 0x399 + 0x370 Human Torque Replay。
- 风险边界明确：默认 OFF、attempt 限制、cooldown、raw clamp、实车安全流程。
