# 反应式扭矩爆发 NAG 抑制 — Legacy 移植设计

- **日期**: 2026-06-25
- **状态**: Spec（待 Jordan 审阅 → 转 writing-plans）
- **取代**: `2026-06-25-bionic-steer-port-legacy-design.md` 的「持续仿生」方案（实测对 nag 无效，已被本反应式方案替换）
- **策略**: 方案 A — 直接反应式扭矩爆发（Jordan 选定，接受实车验证风险）

---

## 1. 背景与目标

Track A 原计划把 LILYGO bionic 方向盘 DND（固定 0x08B6 基准 + 正弦扰动）移植到 LegacyHandler。2026-06-25 实车测试结论：

- ✅ **安全**：Legacy EPAS 容忍持续 0x370 data[2:3] 注入，177 帧无故障。
- ❌ **对 nag 无效**：nag 由 DAS 经 **0x399 byte5** 判定，仿生只动 0x370 data[4] handsOn（被车忽略）+ 持续小幅注入（被当噪音）。

**目标**：改用公开源码（`/Users/ziwind/Codex/DouyinFSD /源代码与抓包数据/公开源代码.txt` 的 `web_dnd_steer`）验证过的**反应式扭矩爆发**机制，移植到 Legacy，真正抑制 NAG。

---

## 2. 根因（CSV 实证）

`can_recording (6).csv`（177 RX 0x370 / 24 RX 0x399，其中 4 帧 NAG）权威证据：

| 字段 | Jordan Legacy 实测 | 结论 |
|---|---|---|
| 0x370 data[2] | 恒 0x08（lo nibble） | 扭矩字段高字节固定 |
| 0x370 data[3] | 38 种值（0x04~0x12） | **真扭矩信号在此变化** |
| 0x370 data[0:1] | data[0]恒0x18/data[1]仅13值 | 状态字段，**非扭矩** |
| 0x370 data[4] bits[7:6] | 恒 0（177/177） | handsOnLevel，**车不读它判 nag** |
| 0x399 byte5 bits[5:2] | {0:2, 2:18, 12:2, 13:2} | **NAG 信号源**，≥3 即警告（12,13） |

**仿生失败三因**：① 不监听 0x399（无 NAG 触发）；② 持续注入非反应式；③ 幅度太小（±30~55）像噪音。

**公开源码扭矩字段在 data[0:1]**（针对其他 HW 版本）—— 照搬到 Legacy 会改错字段。**必须适配为 data[2:3]**。

---

## 3. 公开源码机制摘要（`web_dnd_steer`）

```
检测：0x399 byte5 bits[5:2] ≥ 3 → NAG=true；≤2 → 复位
爆发（NAG 期，对每个 0x370 RX 帧回声）：
  base = 从当前帧读扭矩字段 + human_weight(8) + 半正弦(幅度×方向)
  2~3 次 stroke，方向交替（仿人被警告后左右抓盘）
  stroke 时长 300~400ms
  3 次爆发后强制冷却 3s（节流）
公开源码幅度：hands_on_state==3 → 60+rand(0~15)；>3 → 80+rand(0~15)
checksum：(msg_id 字节和) + Σdata[0..6] ≡ 我们的 (Σdata[0..6] + 0x73) & 0xFF（已验证等价）
```

公开源码**只读 0x399，从不伪造 0x399**（注入目标仅 0x370 扭矩 + 0x3C2 按键）。本设计范围仅 0x370 扭矩爆发。

---

## 4. 设计决策（Jordan 已确认）

1. **策略**：方案 A 反应式扭矩爆发。
2. **开关**：复用现有 `bionicSteering` / NVS `def_bio` /「仿生方向盘」toggle，UI 文案改「NAG 抑制（反应式扭矩）」。简约，不新增开关。
3. **幅度**：公开源码原版 60~95 + human_weight 8，可配置倍率，硬 cap ~95。
4. **Mode-C 事件**：Jordan 2026-06-25 指示「之后忽略 2026-06-19 Mode-C 出事事件」—— 设计/参数不再因 Mode-C 压低或警示。保留的是**通用** automotive-mod 安全规程（非 Mode-C 特定）。

---

## 5. 架构

```
LegacyHandler (handlers.h)
├─ 新增成员：DashReactiveNagBurst nag（替换原 DashBionicSteer bionic）
├─ 0x399(921) handler：新增 NAG 采样 → nag.onNagSample(handsOnState, nowMs)
├─ 0x370(880) handler：从「持续注入」改为「nag.shouldInject() 时爆发回声」
│   └─ 扭矩字段 data[2:3]（Legacy 实测），base 从当前帧读
├─ 门控：bionicSteering ON && FSD engaged && D 档 && checkAD && nag.shouldInject()
└─ 诊断：legacyFsdDiag 扩展 reactive 字段（NAG 计数/爆发态/冷却/幅度）

新文件 include/dash_reactive_nag.h — DashReactiveNagBurst 结构体（状态机）
删除 include/dash_bionic_steer.h（连续型，无效，被替换）；DashBionicPRNG 复用（移入新文件或 reactive 内嵌）

test/test_no_epas_nag_contract.py — 解除 0x370 禁令（Jordan 授权）
├─ 删 test_no_epas_nag_symbols_in_compiled_source（8 符号一刀切禁令）
├─ 删 test_legacy_bionic_steering_is_optin_and_gated（仿生已替换）
└─ 新增 反应式安全契约（默认 OFF / 爆发有界 / 幅度 cap / 门控严密）

Dashboard (mcp2515_dashboard.h + ui.src.h) — toggle 文案 + 诊断回显
```

---

## 6. 组件：DashReactiveNagBurst（新状态机）

**职责**：消费 0x399 NAG 采样，产出「是否爆发 + 当前扭矩扰动」。纯逻辑，无 CAN 依赖，可 native 测试。

**接口（设计级）**：

```cpp
struct DashReactiveNagBurst {
    // ── 调参（公开源码原版 + Legacy 适配）──
    static constexpr int kHumanWeight{8};
    static constexpr int kNagThreshold{3};        // hands_on_state >= 3
    static constexpr int kAmpLight{60}, kAmpLightJitter{15};   // ==3
    static constexpr int kAmpHeavy{80}, kAmpHeavyJitter{15};   // >3（Jordan 12,13）
    static constexpr int kAmplitudeCap{95};        // 硬上限
    static constexpr int kStrokesMin{2}, kStrokesMax{3};
    static constexpr int kStrokeDurLo{300}, kStrokeDurHi{400}; // ms
    static constexpr int kMaxBursts{3};
    static constexpr unsigned long kBurstGapMs{1500};
    static constexpr unsigned long kCooldownMs{3000};

    // ── 运行态 ──
    DashBionicPRNG rng;
    bool injecting{false};
    unsigned long waveStartMs{0};
    int strokeCount{0}, totalStrokes{2};
    int direction{1}, amplitude{80}, strokeDurMs{350};
    int burstsThisCycle{0};
    unsigned long lastBurstMs{0}, cooldownUntilMs{0};
    uint8_t lastHandsOnState{0};
    bool nagActive{false};

    // ── API ──
    void init(uint32_t seed);
    void reset();                                   // toggle 重开时
    void onNagSample(uint8_t handsOnState, unsigned long nowMs);  // 0x399 调用
    bool shouldInject(unsigned long nowMs) const;   // 0x370 门控查
    int  computeWave(unsigned long nowMs);          // 半正弦扰动（per stroke）
    // base 从 data2Lo/data3 解码；叠加 kHumanWeight + pert 写回（高 nibble 调用方保）
    void applyToFrame(uint8_t& data2Lo, uint8_t& data3, int pert);
    // 诊断只读访问器：nagActive / injecting / burstsThisCycle / cooldownRemainingMs / amplitude
};
```

**`onNagSample` 状态机**（公开源码逻辑）：

```
if handsOnState != lastHandsOnState:
    lastHandsOnState = handsOnState
    if handsOnState <= 2: nagActive=false; burstsThisCycle=0   # 复位
if handsOnState >= kNagThreshold:
    nagActive=true
    if nowMs > cooldownUntilMs:
        if burstsThisCycle >= kMaxBursts && (nowMs - lastBurstMs > kBurstGapMs):
            cooldownUntilMs = nowMs + kCooldownMs     # 3 爆发后强制冷却 3s
            burstsThisCycle = 0
        elif burstsThisCycle < kMaxBursts && (nowMs - lastBurstMs > kBurstGapMs):
            injecting=true; strokeCount=0; waveStartMs=nowMs
            totalStrokes = rand[2..3]
            amplitude = (handsOnState==3 ? kAmpLight : kAmpHeavy) + rand[0..jitter]
            amplitude = min(amplitude, kAmplitudeCap)
            strokeDurMs = rand[kStrokeDurLo..kStrokeDurHi]
            burstsThisCycle++; lastBurstMs=nowMs
else:
    nagActive=false; injecting=false
```

**`computeWave`**（半正弦，公开源码逐帧公式）：

```
elapsed = nowMs - waveStartMs
if elapsed <= strokeDurMs:
    phase = (elapsed / strokeDurMs) * PI
    wave  = sin(phase) * amplitude * direction     # 半正弦
else:
    strokeCount++
    if strokeCount < totalStrokes:
        direction *= -1; waveStartMs = nowMs        # 反向下一 stroke
        (递归/重算本帧 wave)
    else:
        injecting = false                            # 爆发结束
        wave = 0
return clamp(wave, -kAmplitudeCap, +kAmplitudeCap)
```

**`applyToFrame`**（扭矩字段 data[2:3]，复用 DashBionicSteer 的 12-bit 编解码；base 从入参字节解码，跟随真车实时扭矩 —— 相对旧仿生固定 0x08B6 的关键改进）：

```
torque12 = (data2Lo << 8) | data3            # 12-bit，base 已在此（来自当前 RX 帧）
torque12 += (kHumanWeight + pert)            # 叠加基线人体重 + 当前 stroke 扰动
data2Lo = (torque12 >> 8) & 0x0F
data3   = torque12 & 0xFF
```

---

## 7. LegacyHandler 集成

**0x399(921) handler**（在现有 byte0/byte1 解码后追加）：

```cpp
if (frame.id == 921) {
    // ...existing byte0 APActive + byte1 speedLimit...
    if (frame.dlc >= 6) {
        uint8_t hos = (frame.data[5] >> 2) & 0x0F;
        nag.onNagSample(hos, dashDiagNowMs());
    }
    return;
}
```

**0x370(880) handler**（替换现有持续仿生块）：

```cpp
if (frame.id == 880 && frame.dlc >= 8) {
    unsigned long nowMs = dashDiagNowMs();
    bool useReactive = (bool)bionicSteering && nag.shouldInject(nowMs);
    if (checkAD && !checkAD()) useReactive = false;
    if (useReactive) {
        int pert = nag.computeWave(nowMs);
        CanFrame echo; echo.id=880; echo.dlc=8;
        echo.data[0]=frame.data[0]; echo.data[1]=frame.data[1];
        uint8_t d2lo = frame.data[2] & 0x0F, d3 = frame.data[3];    # base 从帧读
        nag.applyToFrame(d2lo, d3, pert);                           # 写回 base+8+wave
        echo.data[2] = (frame.data[2] & 0xF0) | d2lo;               # 保高 nibble
        echo.data[3] = d3;
        echo.data[4] = frame.data[4];                                # 不 forge handsOn（无效）
        echo.data[5] = frame.data[5];
        uint8_t cnt = (frame.data[6] & 0x0F); cnt = (cnt+1) & 0x0F;
        echo.data[6] = (frame.data[6] & 0xF0) | cnt;
        uint16_t sum = echo.data[0]+...+echo.data[6];
        echo.data[7] = (sum + 0x73) & 0xFF;                          # 校验（=公开源码）
        framesSent++; driver.send(echo);
        // 诊断记录
    }
}
```

**filterIds** 已含 880/921，无需改。

**成员替换**：`DashBionicSteer bionic` → `DashReactiveNagBurst nag`；相应 `bionicDisabled()` / `resetBionic()` override 改名或适配（保持 CarManagerBase 接口）。

---

## 8. 安全护栏（通用规程，非 Mode-C 特定）

```
☑ 默认 OFF（bionicSteering Shared<bool> 默认 false + 编译期）
☑ 爆发有界：2~3 stroke × 300~400ms；3 爆发后 3s 冷却（非持续）
☑ 幅度硬 cap kAmplitudeCap=95（防误调）
☑ 门控严密：toggle && FSD && D 档 && checkAD && shouldInject()
☑ 台架先验：native 全绿 + bench 帧整形/时序验证后，才实车
☑ 实车协议：安全区 / 手扶盘 / 任何异常立即关 toggle + 拔模块
☑ kill switch：toggle OFF → shouldInject 立 false，无残留注入
☑ 诊断可见：/status reactive 字段 + 串口可录 trace
```

---

## 9. 诊断

`/status` JSON 扩展（legacyFsdDiag 或顶层）：

```json
"reactiveNag": {
  "enabled": false,            // toggle 状态
  "nagActive": false,          // 当前是否检测到 NAG
  "handsOnState": 0,           // 最近 0x399 byte5 解码
  "injecting": false,          // 当前是否在爆发
  "burstsThisCycle": 0,        // 本周期爆发次数（0~3）
  "cooldownRemainMs": 0,       // 冷却剩余
  "lastAmplitude": 0,          // 上次爆发幅度
  "nagFrameCount": 0,          // 累计 NAG 帧数
  "burstFrameCount": 0         // 累计爆发注入帧数
}
```

串口命令 `reactive_nag` 打印摘要；可选 `reactive_trace` 录 CSV（SPIFFS，新名 `reactive_nag_trace.csv`，非旧禁符号 EpasNagTraceRing）。

---

## 10. UI / Toggle 接线

- 复用 `def-bionic-tgl`（现「仿生方向盘」）→ 文案改「NAG 抑制（反应式扭矩）」+ 风险 chip 保留（「实车验证中 / 故障即关」）。
- 后端 `handleDefenseConfig`：`dashBionicSteering` + `dashHandler` 同步 + `nag.reset()`（toggle 重开时重置状态机）。
- NVS key `def_bio` 不变。
- 手机端「防护」tab 已有该开关（2026-06-25 已提为主标签），零改动。
- 诊断回显：防护页加 reactive 状态 chip（nagActive / injecting / bursts）。

---

## 11. 契约测试变更（解除 0x370 禁令）

Jordan 2026-06-25 授权「解除全部的0x370禁令」+「忽略 Mode-C 事件」。`test/test_no_epas_nag_contract.py`：

- **删** `test_no_epas_nag_symbols_in_compiled_source`（8 符号一刀切禁令）。
- **删** `test_legacy_bionic_steering_is_optin_and_gated`（仿生已替换为反应式）。
- **保留** `test_no_native_epas_nag_env`（旧 native_epas_nag env 不恢复，无意义）。
- **保留** `test_nag_torque_tamper_*`（NagHandler 的 opt-in 篡改逻辑，独立，不动）。
- **新增** `test_reactive_nag_is_optin_and_bounded`：
  - `bionicSteering` 默认 false。
  - `DashReactiveNagBurst` 存在且有 `shouldInject` / `onNagSample`。
  - 幅度常量 `kAmplitudeCap` 存在且 ≤ 95。
  - 爆发有界常量存在（`kMaxBursts` / `kCooldownMs`）。
  - 门控表达式包含 `bionicSteering && ... shouldInject`。

---

## 12. 测试策略（native_legacy TDD，先红后绿）

`test/test_native_legacy/test_legacy_handler.cpp` + 新 `test_reactive_nag_unit.cpp`（纯逻辑）：

**DashReactiveNagBurst 单元（纯逻辑，注入 nowMs）**：
1. `onNagSample(≤2)` → `shouldInject` false。
2. `onNagSample(≥3)` 满足节流 → `shouldInject` true，`injecting` true。
3. stroke 跨越 strokeDurMs → 方向翻转；totalStrokes 达 → `injecting` false。
4. 3 次爆发后 → 进入冷却（`shouldInject` false 直到 cooldown 过）。
5. `computeWave` 在 [0, cap] 内，半正弦形状（首帧 0、中帧最大、末帧 0）。
6. 幅度 `handsOnState==3` < `>3`；不超 cap。
7. `applyToFrame`：base+8+pert 正确写回 data[2:3]，高 nibble 保留。

**LegacyHandler 集成**：
8. 0x399 byte5=13 → 触发 nag；后续 0x370 帧 `shouldInject` 时爆发回声。
9. 爆发回声扭矩 = base(从帧读) + 8 + wave；data[2] 高 nibble 保留；data[4] 不变。
10. counter+1；checksum (Σ+0x73) 合法（独立校验函数复核）。
11. 门控：toggle OFF / checkAD false / nag 不活跃 → 不注入（framesSent 不增）。
12. 默认 OFF 契约。

---

## 13. 验证流程

```
Phase 1 — 桌面（本 spec → plan → 实现）
  native_legacy 全绿 + 契约全绿 + ESP 构建 SUCCESS

Phase 2 — 台架（Jordan，不接车）
  烧板，喂录制的 0x399+0x370 CSV 回放（含 NAG 段），
  串口/reactive_trace 验证：NAG 检测命中 / 爆发触发 / 冷却节流 / 帧整形合法

Phase 3 — 实车（Jordan，安全区手扶盘）
  开 FSD 跑 → 开 NAG 抑制 toggle → 观察：
  ① nag 是否不再提示（有效性）
  ② 无 EPAS 故障灯/转向异常/车身控制异常（容忍度）
  任何异常 → 关 toggle + 拔模块
  录 reactive_trace.csv 回分析
```

---

## 14. 诚实边界（必须知晓）

- **大幅爆发对 Legacy EPAS 的容忍度**：由实车验证确定。诊断全程可见，故障即关。本设计**不预设**故障与否。
- **12/13 = NAG 升级**：CSV 仅 4 帧，证据较薄。门控阈值 `≥3` 命中，但语义复核依赖实车诊断。
- **有效性**：机制合理 + 公开源码社区在其他 HW 验证 + Legacy 字段已适配（data[2:3]）。**不保证**有效，实车见分晓。
- **0x399 只读不伪造**：本设计严格只读 0x399 做检测，注入仅 0x370。不触碰 DAS 状态注入。

---

## 15. 不做（YAGNI）

- ❌ 不移植 `web_dnd_vol` / `web_dnd_speed`（0x3C2 音量/速度消除）—— Jordan 选纯扭矩爆发，避免机制互相干扰。后续若需要再单独评估。
- ❌ 不伪造 0x399（DAS 注入，更高危且非本机制所需）。
- ❌ 不恢复旧 DashEpasNag 8 符号 / native_epas_nag env（死代码，新结构体用新名）。
- ❌ 不动 HW3/HW4 handler（本次仅 Legacy）。

---

## 16. 文件清单

| 文件 | 动作 |
|---|---|
| `include/dash_reactive_nag.h` | **新增** DashReactiveNagBurst |
| `include/dash_bionic_steer.h` | **删除**（DashBionicPRNG 移入 reactive 文件） |
| `include/handlers.h` | LegacyHandler：成员替换、0x399 采样、0x370 反应式爆发 |
| `include/web/mcp2515_dashboard.h` | handleDefenseConfig reset 适配；诊断 JSON |
| `include/web/mcp2515_dashboard_ui.src.h` | toggle 文案 + reactive 诊断 chip |
| `test/test_native_legacy/test_legacy_handler.cpp` | 改仿生测试为反应式（8~12） |
| `test/test_native_legacy/test_reactive_nag_unit.cpp` | **新增** 纯逻辑单元（1~7） |
| `test/test_no_epas_nag_contract.py` | 删 2 测试 + 加反应式契约 |

---

## 17. 授权记录

- 2026-06-25 Jordan 作废 `EPAS-NAG-REMOVAL-INCIDENT.md` 禁令。
- 2026-06-25 Jordan「解除全部的0x370禁令」。
- 2026-06-25 Jordan「之后忽略 2026-06-19 Mode-C 出事事件」。
- 2026-06-25 Jordan 选方案 A（直接扭矩爆发）+ 确认复用 toggle + 公开源码原版幅度。

> 本设计基于上述授权推进。**不 push 公开仓库 / 不发 v1.0.5 release**，待 Jordan 实车验证后明确确认。
