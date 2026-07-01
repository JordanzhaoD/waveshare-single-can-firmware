# 反应式 NAG 抑制 v2 — Proactive 基线 + 持续扭矩几何（A+B+C）

- **日期**: 2026-06-26
- **状态**: Spec（待 Jordan 审阅 → 转 writing-plans）
- **取代/升级**: v1（`2026-06-25-reactive-nag-torque-burst-legacy-design.md`）。v1 实车测得**部分有效**（提醒降频但未消除）；v2 目标「大部分清除」。
- **策略**: A+B+C 全上（Jordan 2026-06-26 选定）。

---

## 1. 背景：v1 实车结果与两个失效模式

v1（反应式零均值正弦扭矩爆发）2026-06-26 实车测试 + 3 段录音（`can_recording (8/9/10).csv`）+ Jordan 体感结论：

- ✅ **部分有效**：Jordan 体感「提醒没那么频繁了」——爆发清除了一部分 NAG episode（降频已证）。
- ❌ **未消除**：两个独立失效模式限制清除率：
  1. **命中率（rec 8）**：3-burst-3s 冷却误时 —— NAG 清除时 C-1 只复位 `burstsThisCycle`，**没复位 `cooldownUntilMs`/`lastBurstMs`** → 下段 NAG 撞陈旧冷却 → NAG 期间 0 TX。
  2. **命中即清率（rec 9）**：爆发在 NAG 期间发了（38 TX 在窗口内），但 hos 仍 5→8 升级，4s+ 才因人握盘清除。

**rec 9 关键洞察**：v1 爆发是**零均值正弦抖动**（+峰、-峰、+峰，积分≈0）。真司机握盘是**持续同向扭矩**（稳持，积分大）。推断 Jordan 的 Legacy DAS 按**持续扭矩积分**判 hands-on —— 零均值抖动「看不见」。这解释 rec 9（发了但没清）。公开源码的抖动在其他 Tesla 有效，因那些车 DAS 判据不同。

**目标（Jordan）**：大部分清除（偶尔提示但很少）。

---

## 2. v2 设计：三模式 + 持续扭矩

### 2.1 三模式状态机
```
toggle ON + APActive 时，按 0x399 hos 分模式：
  PROACTIVE  hos ≤ 2（无NAG） → 周期性持续轻扭矩（每 2~5s 随机一次短 hold），DAS 持续看到「人在」→ 预防 NAG 触发
  REACTIVE   hos ≥ 3（NAG）   → 持续强扭矩 hold（sine 抖动 + DC 偏置），hos=3 即触发
  IDLE       toggle OFF 或 非AP → 不注入
```
- hos ≤2 → ≥3（NAG 起）：取消 proactive，立即 REACTIVE。
- hos ≥3 → ≤2（NAG 清）：**全复位**（reactive 状态 + 节流时间戳 + proactive 调度），下段 NAG 立刻能爆发，proactive 恢复。

### 2.2 REACTIVE 持续扭矩几何（A 核心）
- **零均值正弦 → 持续偏置扭矩**：每帧回声扭矩 = `base + DC 同向偏置 + 小正弦抖动`，**积分 > 0**，像真人稳持方向盘。
  - DC 偏置：强同向持续分量（让积分显著为正）—— 主幅度。
  - 小抖动：±jitter 模拟人手微动（非纯平台，更像人）。
- **统一强档**：删 hos==3 轻档分级（v1 kAmpLight），全部强档。
- **峰值有界**：DC 偏置为主幅度（~60~95 raw），抖动 ±~15 raw，总扰动峰值封顶 ~kAmplitudeCap(95~110) raw —— 与 v1 同量级，只是积分非零。具体数值 + cap 由 plan 定，确保不超 EPAS 安全范围。
- **早触发**：hos ≥ 3 即爆发（kNagThreshold=3 不变），不等升级到高级。

### 2.3 节流重做（修 rec 8）
- **删 3-burst-3s-cooldown**（rec 8 bug 源）。
- **NAG 期内**：每次持续 hold 爆发 ~1s，爆发间隔 ~800ms（`kReactiveGapMs`）。NAG 持续 5s → ~3 次密集 hold 压制。
- **NAG 清除即全复位**：`onNagSample` 检测 hos≤2 时清 reactive 状态 + 节流时间戳 + proactive 调度。
- **无跨 episode 残留**：陈旧冷却/间隔不再挡下段 NAG。

### 2.4 PROACTIVE 基线（B）
- hos≤2 + AP + toggle 时，**每 2~5s 随机**（`nextProactiveMs`）发一次**轻持续 hold**（幅度 30~55 + 小 DC 偏置 + 抖动，2~3 stroke ~600ms）。
- 非每帧注入（周期性短 hold），总线压力可控。
- 目的：让 DAS 一直判「手在盘」（积分持续为正），NAG 从源头少触发（预防 > 反应）。

### 2.5 data[4] handsOn forge（C）
- **所有回声**（proactive + reactive）设 `data[4] bits[7:6] = 01`（handsOnLevel=1）。
- v1 停止了 forge（因早期数据说车不读 data[4]）；v2 重加 —— **扭矩持续注入 + 握手标志组合**，验证是否比纯扭矩清除率更高。
- 其他字节规则不变（见 2.6）。

### 2.6 帧 byte-exact 规则（不变，只改扭矩值 + data[4]）
```
echo.data[0..1] = frame.data[0..1]（透传）
echo.data[2]    = (frame.data[2] & 0xF0) | 新扭矩高 nibble   ← 保高 nibble
echo.data[3]    = 新扭矩低 byte
echo.data[4]    = frame.data[4] | 0x40                        ← C: forge handsOnLevel=1
echo.data[5]    = frame.data[5]（透传）
echo.data[6]    = (frame.data[6] & 0xF0) | ((counter+1)&0x0F) ← counter+1，保高 nibble
echo.data[7]    = (Σdata[0..6] + 0x73) & 0xFF                  ← checksum
```

---

## 3. 架构改动

```
DashReactiveNagBurst（dash_reactive_nag.h）重做：
├─ 三模式状态（mode: IDLE/PROACTIVE/REACTIVE）
├─ 统一 hold 几何：DC 偏置 + 小正弦抖动（proactive 轻 / reactive 强）
├─ 删 kMaxBursts/kCooldownMs/kBurstGapMs；加 kReactiveGapMs(800) / kProactiveIntervalLo/Hi(2000/5000)
├─ onNagSample(hos, nowMs)：定模式 + proactive 调度 + reactive 触发；hos≤2 全复位
├─ shouldEcho(nowMs) → bool：本帧是否注入（proactive hold 帧或 reactive hold 帧）
├─ computeHold(nowMs) → int：DC偏置 + 抖动扭矩扰动
├─ applyToFrame(d2lo, d3, pert)：base + pert 写回（同 v1）
└─ 诊断：mode / proactiveWiggles / reactiveBursts / echoSent（NVS 持久化）

LegacyHandler（handlers.h）0x370 块：
├─ useReactive = bionicSteering && APActive && nag.shouldEcho(nowMs)
├─ checkAD 门控保留
├─ pert = nag.computeHold(nowMs); applyToFrame; data[4]|=0x40（C）；counter+1；checksum
└─ nag.notifyEchoSent()

0x399 块不变（onNagSample 调用同 v1）

诊断（dashboard.h）：reactive_nag + /status 加 mode + proactiveWiggles + reactiveBursts；修 NVS 持久化（v1 counter=0 flush bug）
```

---

## 4. 安全边界

- ⚠️ **注入量大幅增加**（proactive 周期 hold + reactive 持续 hold）。v1 单次 45/38 TX 无 EPAS 故障，但 v2 更密集（NAG 5s → ~3 reactive hold + 期间 proactive）。
- **首启必须安全区实车验 EPAS 不报故障**：Jordan 手扶盘，任何异常（故障灯/转向异样/车身控制）立即关 toggle + 拔模块。
- 帧 byte-exact 规则不变（只改扭矩值 + 加 data[4] forge）—— 不引入新帧格式风险。
- 默认 OFF；门控严密（toggle + APActive + checkAD + shouldEcho）；kill switch（toggle OFF 即停）。

---

## 5. 诊断升级（修 v1 counter=0 bug）

v1 实车 reactive_nag 计数全 0（NVS flush 没存住或开机没装载）。v2 修：
- 计数器：`mode`（当前模式）、`proactiveWiggles`、`reactiveBursts`、`echoSent`、`nagSamples`。
- NVS 持久化：每 ~2s flush（缩短 v1 的 5s，减 abrupt 断电损失）+ 开机装载 + `reactive_nag_reset` 命令。
- reactive_nag 串口 + /status JSON 暴露全部。
- 验证：台架模拟 NAG → 计数递增 → 断电 → 重启 → 计数仍在（端到端测持久化）。

---

## 6. 测试策略（native TDD）

`test_native_reactive_nag`（重写 v1 的 8 用例为 v2）：
1. IDLE：toggle OFF / 非AP → shouldEcho false。
2. PROACTIVE：hos≤2 + AP → 周期性 shouldEcho true（2~5s 节奏），hold 扰动在轻幅度档。
3. REACTIVE：hos≥3 → 立即 shouldEcho true，hold 扰动在强幅度档。
4. **持续几何（核心）**：REACTIVE 多帧 computeHold 的积分 > 0（验 DC 偏置生效，区别于 v1 零均值）。
5. **NAG 清除全复位（修 rec 8）**：hos≥3→≤2 → reactive 状态清 + 节流时间戳清 + proactive 恢复；下段 NAG 立即爆发（无陈旧冷却挡）。
6. NAG 期内 ~800ms 间隔（不超密）。
7. data[4] forge：回声 data[4] bits[7:6]==01。
8. 帧 byte-exact：counter+1、checksum +0x73、data[2] 高 nibble 保。
9. 模式切换：proactive 进行中 NAG 起 → 切 REACTIVE 取消 proactive。

`test_native_legacy`（集成）：0x399 hos 驱动模式 → 0x370 回声几何 + data[4] forge + 门控。

契约 `test_no_epas_nag_contract.py`：更新 `test_reactive_nag_is_optin_and_bounded` 适配 v2（默认 OFF / shouldEcho 门 / 删 kMaxBursts-kCooldown 引用 / 加 kReactiveGapMs）。

---

## 7. 验证流程

```
Phase 1 桌面：native 全绿 + 契约全绿 + ESP 构建 SUCCESS
Phase 2 台架：回放 can_recording(8/9/10).csv（含 NAG 段）→ reactive_nag 验
              模式切换（PROACTIVE↔REACTIVE）/ proactive 周期 / reactive 持续几何 / 计数持久化（断电复验）
Phase 3 实车（Jordan，安全区，手扶盘，异常即关）：
              开 toggle → FSD → 观察 NAG 提示频率是否较 v1 进一步下降 + EPAS 无故障
              录 reactive_nag + CAN → 对比 v1 清除率
```

---

## 8. 诚实边界

- **持续扭矩几何（DC 偏置）是假设**：rec 9 零均值没清除 → 推断要持续；实车验证才知是否清除率真比 v1 高。
- **proactive 基线能否减 NAG 触发**待实车（公开源码在其他车有效，Legacy 未知）。
- **data[4] forge 单做过无效**；扭矩+组合是否有效是实验。
- **注入量大增**，EPAS 对密集持续 hold 的容忍度 = 实车验证（v1 单次无故障不代表 v2 密集无故障）。
- 若 v2 实车仍大部不清除 → 回退到「v1 + 仅修 rec 8 冷却 bug」（保守增量），或承认 Legacy NAG 非 0x370 可抑制。

---

## 9. 不做（YAGNI）

- ❌ 不动 0x399（只读）、不伪造 DAS 状态。
- ❌ 不碰 HW3/HW4 handler（仅 Legacy）。
- ❌ 不引入 0x3C2 dismiss（本 spec 聚焦 0x370 路径优化；0x3C2 是另一条路，未来单独评估）。
- ❌ 不恢复旧 DashEpasNag 8 符号。

---

## 10. 文件清单

| 文件 | 动作 |
|---|---|
| `include/dash_reactive_nag.h` | **重写** v2：三模式 + 持续 hold 几何 + 删冷却节流 + proactive 调度 + data[4] forge 接口 |
| `include/handlers.h` | LegacyHandler 0x370 块：shouldEcho + computeHold + data[4]\|=0x40 |
| `include/web/mcp2515_dashboard.h` | reactive_nag + /status 加 mode/proactiveWiggles/reactiveBursts；修 NVS flush（2s） |
| `test/test_native_reactive_nag/test_main.cpp` | **重写** v2 用例（9 条） |
| `test/test_native_legacy/test_legacy_handler.cpp` | 更新集成测（v2 几何 + data[4] forge） |
| `test/test_no_epas_nag_contract.py` | 更新反应式契约（v2 常量/门控） |

---

## 11. 授权记录

- 2026-06-25 Jordan 解除 0x370 禁令 + 忽略 Mode-C 事件（继续生效）。
- 2026-06-26 Jordan 实车确认 v1 部分有效，选 v2 = A+B+C 全上，目标「大部分清除」。

> 不 push / 不发 release，待 Jordan v2 实车验证后确认。
