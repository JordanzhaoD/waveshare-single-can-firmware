# 反应式扭矩爆发 NAG 抑制 — Legacy 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把公开源码 `web_dnd_steer` 的反应式扭矩爆发机制移植到 waveshare 单 CAN LegacyHandler，真正抑制 NAG（取代实测无效的持续仿生）。

**Architecture:** 新增 header-only 状态机 `DashReactiveNagBurst`（纯逻辑，`nowMs` 注入可测）：0x399 byte5 bits[5:2]≥3 检测 NAG → 启动有界扭矩爆发（2~3 stroke 半正弦，3 爆发后 3s 冷却）；LegacyHandler 的 0x370 块从「持续仿生」改为「爆发期回声」，扭矩写 data[2:3]（Legacy 实测字段，base 从帧读 + human_weight 8 + wave）。删 `DashBionicSteer`（无效）。解除 0x370 禁令契约，换反应式安全契约。

**Tech Stack:** ESP-IDF + PlatformIO（C++17），Unity native 测试，Python pytest 契约测试，TWAI CAN bus。

**Spec:** `docs/superpowers/specs/2026-06-25-reactive-nag-torque-burst-legacy-design.md`

**Git 约定:** 每任务一 commit，`git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit`。在本地 main 上做（Jordan 已确认）。不 push。

**关键事实（写代码前必读）:**
- 0x370 扭矩字段 = `(data[2] & 0x0F) << 8 | data[3]`（Legacy 实测，CSV 证）。data[2] 高 nibble 是别的信号，**必须保留**。
- 0x370 校验 = `(Σdata[0..6] + 0x73) & 0xFF`，写入 data[7]。counter = `(data[6] & 0x0F + 1) & 0x0F`，写回 data[6] 低 nibble（保高 nibble）。
- NAG 源 = 0x399(921) byte5 bits[5:2]（`hands_on_state`），≥3 为警告。**只读不伪造**。
- `dashDiagNowMs()` 在 native 是「每调用 +1」计数器（非真毫秒）；故 `DashReactiveNagBurst` 所有时间方法**显式接收 `nowMs` 参数**，单元测试注入精确值，集成测试不测精确时序。
- toggle `bionicSteering`（NVS `def_bio`）**复用**，语义改「NAG 抑制」。`handleDefenseConfig` 已在开 toggle 时调 `resetBionic(millis())`，无需改。

---

## 文件结构

| 文件 | 责任 | 动作 |
|---|---|---|
| `include/dash_reactive_nag.h` | DashReactiveNagBurst 状态机（纯逻辑）+ DashReactivePRNG | **新增** |
| `include/dash_bionic_steer.h` | 旧 DashBionicSteer（无效） | **删除** |
| `include/handlers.h` | LegacyHandler：成员替换、0x399 采样、0x370 反应式回声、override 适配 | 修改 |
| `include/web/mcp2515_dashboard.h` | 串口 `reactive_nag` 命令、/status reactive JSON、stale 注释 | 修改 |
| `include/web/mcp2515_dashboard_ui.src.h` | toggle 文案 + risk chip 文案 | 修改 |
| `test/test_native_reactive_nag/test_main.cpp` | DashReactiveNagBurst 纯单元测试 | **新增** |
| `test/test_native_legacy/test_legacy_handler.cpp` | 删 7 仿生测试，加反应式集成测试 + makeDasFrame helper | 修改 |
| `test/test_native_bionic_steer/` | 旧 DashBionicSteer 单元测试 | **删除** |
| `platformio.ini` | `[env:native_bionic_steer]` → `[env:native_reactive_nag]` | 修改 |
| `test/test_no_epas_nag_contract.py` | 删 2 测试，加反应式安全契约 | 修改 |

---

## Task 1: DashReactiveNagBurst 状态机 + 单元测试环境

**Files:**
- Create: `include/dash_reactive_nag.h`
- Create: `test/test_native_reactive_nag/test_main.cpp`
- Modify: `platformio.ini:135-139`（重命名 env）
- Delete: `test/test_native_bionic_steer/`（整目录）

- [ ] **Step 1: 重命名 native 测试 env**

`platformio.ini` 第 135-139 行，把 `[env:native_bionic_steer]` 改为 `[env:native_reactive_nag]`，`test_filter` 改为 `test_native_reactive_nag`：

```ini
[env:native_reactive_nag]
platform = native
extra_scripts = pre:scripts/platformio_native_env.py
build_flags = -std=c++17 -DNATIVE_BUILD
test_filter = test_native_reactive_nag
```

- [ ] **Step 2: 删除旧 bionic 单元测试目录**

```bash
rm -rf test/test_native_bionic_steer
```

- [ ] **Step 3: 写 DashReactiveNagBurst 单元测试（先红）**

Create `test/test_native_reactive_nag/test_main.cpp`：

```cpp
#include <unity.h>
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

// 1. NAG 未触发（hands_on_state<=2）→ 不注入
void test_no_nag_no_inject()
{
    DashReactiveNagBurst n;
    n.init(12345);
    n.onNagSample(2, 100);
    TEST_ASSERT_FALSE(n.shouldInject(100));
    TEST_ASSERT_FALSE(n.isNagActive());
}

// 2. NAG 触发（hands_on_state>=3）→ 启动爆发，应注入
void test_nag_starts_burst()
{
    DashReactiveNagBurst n;
    n.init(1);
    n.onNagSample(13, 100);   // Jordan 实测 NAG 值 12,13
    TEST_ASSERT_TRUE(n.isNagActive());
    TEST_ASSERT_TRUE(n.shouldInject(100));
    TEST_ASSERT_EQUAL(1, n.burstsThisCycle());
}

// 3. 第二次爆发需 1500ms 间隔（burst gap）
void test_burst_gap_enforced()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(13, 100);            // burst #1, lastBurstMs=100
    TEST_ASSERT_TRUE(n.shouldInject(100));
    // 推进到爆发结束（stroke 完成）→ injecting=false。
    // 必须递增 nowMs：computeWave 在 elapsed>strokeDur 时推进一个 stroke 并把
    // waveStartMs 重置为 nowMs，故每步 +500ms（>strokeDur 400）才推进。
    unsigned long t = 100;
    while (n.shouldInject(t)) { n.computeWave(t); t += 500; }
    n.onNagSample(13, 200);            // 距上次 100ms < 1500 → 不启动新爆发
    TEST_ASSERT_FALSE(n.shouldInject(200));
    n.onNagSample(13, 1700);           // 距上次 1600ms > 1500 → 启动 #2
    TEST_ASSERT_TRUE(n.shouldInject(1700));
    TEST_ASSERT_EQUAL(2, n.burstsThisCycle());
}

// 4. 3 爆发后强制 3s 冷却
void test_cooldown_after_three_bursts()
{
    DashReactiveNagBurst n;
    n.init(3);
    unsigned long t = 100;
    for (int burst = 0; burst < 3; ++burst)
    {
        n.onNagSample(13, t);
        TEST_ASSERT_TRUE(n.shouldInject(t));
        unsigned long dt = t;          // 递增 drain（同上）
        while (n.shouldInject(dt)) { n.computeWave(dt); dt += 500; }
        t += 1600;                     // 下一爆发 gap
    }
    TEST_ASSERT_EQUAL(3, n.burstsThisCycle());
    n.onNagSample(13, t);              // 第 4 次 → 触发冷却
    TEST_ASSERT_FALSE(n.shouldInject(t));
    TEST_ASSERT_TRUE(n.cooldownRemainingMs(t) > 0);
    n.onNagSample(13, t + 3100);       // 冷却过后 → 可再爆发
    TEST_ASSERT_TRUE(n.shouldInject(t + 3100));
}

// 5. computeWave 半正弦：首帧≈0、中帧最大、末帧≈0，且不超 cap
void test_wave_half_sine_shape()
{
    DashReactiveNagBurst n;
    n.init(4);
    n.onNagSample(13, 0);
    int first = n.computeWave(0);
    int mid = n.computeWave(175);      // strokeDur~350ms 中点
    int maxSeen = (abs(first) > abs(mid)) ? abs(first) : abs(mid);
    for (unsigned long t = 176; t <= 400; ++t)
    {
        int w = n.computeWave(t);
        if (abs(w) > maxSeen) maxSeen = abs(w);
        TEST_ASSERT_TRUE(abs(w) <= n.amplitudeCap());
    }
    TEST_ASSERT_TRUE(maxSeen > 0);
    TEST_ASSERT_TRUE(maxSeen <= n.amplitudeCap());
}

// 6. 幅度分级：hands_on_state==3 轻（60~75）；>3 重（80~95）；不超 cap
void test_amplitude_tiers_and_cap()
{
    DashReactiveNagBurst n;
    n.init(5);
    n.onNagSample(3, 100);
    int ampLight = n.lastAmplitude();
    TEST_ASSERT_TRUE(ampLight >= 60 && ampLight <= 75);

    DashReactiveNagBurst n2;
    n2.init(6);
    n2.onNagSample(13, 100);
    int ampHeavy = n2.lastAmplitude();
    TEST_ASSERT_TRUE(ampHeavy >= 80 && ampHeavy <= 95);
}

// 7. applyToFrame：base（从帧字节解码）+ human_weight(8) + pert 写回 data[2:3]，高 nibble 保留
void test_apply_to_frame()
{
    DashReactiveNagBurst n;
    n.init(7);
    uint8_t d2lo = 0x08, d3 = 0x12;    // base = 0x0812 = 2066
    n.applyToFrame(d2lo, d3, 50);      // +human_weight 8 + pert 50 = +58 → 2124 = 0x084C
    int16_t out = (int16_t)(((uint16_t)d2lo << 8) | d3);
    TEST_ASSERT_EQUAL_INT16(0x0812 + 8 + 50, out);
    TEST_ASSERT_EQUAL_UINT8(0x08, d2lo);   // 高 nibble 由调用方保，这里 d2lo 是低 nibble
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_no_nag_no_inject);
    RUN_TEST(test_nag_starts_burst);
    RUN_TEST(test_burst_gap_enforced);
    RUN_TEST(test_cooldown_after_three_bursts);
    RUN_TEST(test_wave_half_sine_shape);
    RUN_TEST(test_amplitude_tiers_and_cap);
    RUN_TEST(test_apply_to_frame);
    return UNITY_END();
}
```

- [ ] **Step 4: 跑测试确认失败（红）**

```bash
pio test -e native_reactive_nag
```
Expected: 编译失败 — `dash_reactive_nag.h` 不存在。

- [ ] **Step 5: 实现 DashReactiveNagBurst（转绿）**

Create `include/dash_reactive_nag.h`：

```cpp
#pragma once

// ──────────────────────────────────────────────────────────────
// dash_reactive_nag.h — Reactive NAG-suppression torque burst
// Port of public-source web_dnd_steer, Legacy-adapted.
//
// Mechanism: detect NAG via 0x399 byte5 bits[5:2] >= 3, then inject
// a bounded half-sine torque burst on 0x370 data[2:3] (Legacy torque
// field). 2-3 strokes, 3 bursts then 3 s cooldown.
//
// Pure logic: all time passed explicitly as nowMs (testable; native
// dashDiagNowMs() is a per-call counter, not real ms).
// ──────────────────────────────────────────────────────────────

#include <cstdint>
#include <cmath>

struct DashReactivePRNG
{
    uint32_t s{0xDEADBEEFu};
    void seed(uint32_t v) { s = v ? v : 0xDEADBEEFu; }
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t range(uint32_t lo, uint32_t hi) { return lo + (next() % (hi - lo + 1)); }
};

struct DashReactiveNagBurst
{
    // ── tunables (public-source-faithful + Legacy cap) ──
    static constexpr int kHumanWeight{8};
    static constexpr int kNagThreshold{3};
    static constexpr int kAmpLight{60};
    static constexpr int kAmpLightJitter{15};
    static constexpr int kAmpHeavy{80};
    static constexpr int kAmpHeavyJitter{15};
    static constexpr int kAmplitudeCap{95};
    static constexpr int kStrokesMin{2};
    static constexpr int kStrokesMax{3};
    static constexpr int kStrokeDurLo{300};
    static constexpr int kStrokeDurHi{400};
    static constexpr int kMaxBursts{3};
    static constexpr unsigned long kBurstGapMs{1500};
    static constexpr unsigned long kCooldownMs{3000};

    // ── runtime state ──
    DashReactivePRNG rng;
    bool injecting{false};
    unsigned long waveStartMs{0};
    int strokeCount{0};
    int totalStrokes{2};
    int direction{1};
    int amplitude{80};
    int strokeDurMs{350};
    int burstsThisCycle_{0};
    unsigned long lastBurstMs{0};
    unsigned long cooldownUntilMs{0};
    uint8_t lastHandsOnState{0};
    bool nagActive_{false};
    int lastAmplitude_{0};

    void init(uint32_t seed) { rng.seed(seed); }
    void reset()
    {
        injecting = false;
        strokeCount = 0;
        burstsThisCycle_ = 0;
        lastBurstMs = 0;
        cooldownUntilMs = 0;
        lastHandsOnState = 0;
        nagActive_ = false;
    }

    bool isNagActive() const { return nagActive_; }
    bool shouldInject(unsigned long /*nowMs*/) const { return injecting; }
    int burstsThisCycle() const { return burstsThisCycle_; }
    int lastAmplitude() const { return lastAmplitude_; }
    int amplitudeCap() const { return kAmplitudeCap; }
    unsigned long cooldownRemainingMs(unsigned long nowMs) const
    {
        return (nowMs < cooldownUntilMs) ? (cooldownUntilMs - nowMs) : 0UL;
    }

    // Called from 0x399 handler. handsOnState = (0x399 data[5] >> 2) & 0x0F.
    void onNagSample(uint8_t handsOnState, unsigned long nowMs)
    {
        if (handsOnState != lastHandsOnState)
        {
            lastHandsOnState = handsOnState;
            if (handsOnState <= 2)
            {
                nagActive_ = false;
                burstsThisCycle_ = 0;   // hands back on → reset cycle
            }
        }
        if (handsOnState >= kNagThreshold)
        {
            nagActive_ = true;
            if (injecting)
                return;   // current burst ongoing
            if (nowMs <= cooldownUntilMs)
                return;   // in forced cooldown
            bool gapOk = (lastBurstMs == 0) || ((nowMs - lastBurstMs) > kBurstGapMs);
            if (!gapOk)
                return;
            if (burstsThisCycle_ >= kMaxBursts)
            {
                cooldownUntilMs = nowMs + kCooldownMs;   // 3 bursts done → rest 3 s
                burstsThisCycle_ = 0;
                return;
            }
            // start a burst
            injecting = true;
            strokeCount = 0;
            waveStartMs = nowMs;
            totalStrokes = (int)rng.range(kStrokesMin, kStrokesMax);
            int base = (handsOnState == 3) ? kAmpLight : kAmpHeavy;
            int jitter = (handsOnState == 3) ? kAmpLightJitter : kAmpHeavyJitter;
            amplitude = base + (int)rng.range(0, jitter);
            if (amplitude > kAmplitudeCap)
                amplitude = kAmplitudeCap;
            lastAmplitude_ = amplitude;
            strokeDurMs = (int)rng.range(kStrokeDurLo, kStrokeDurHi);
            direction = (rng.next() & 1u) ? 1 : -1;
            burstsThisCycle_++;
            lastBurstMs = nowMs;
        }
        else
        {
            nagActive_ = false;
        }
    }

    // Half-sine perturbation for current stroke. Returns 0 when burst done
    // (and clears injecting).
    int computeWave(unsigned long nowMs)
    {
        if (!injecting)
            return 0;
        unsigned long elapsed = nowMs - waveStartMs;
        if (elapsed <= (unsigned long)strokeDurMs)
        {
            float phase = ((float)elapsed / (float)strokeDurMs) * (float)M_PI;
            int w = (int)(sinf(phase) * (float)amplitude * (float)direction);
            if (w > kAmplitudeCap) w = kAmplitudeCap;
            if (w < -kAmplitudeCap) w = -kAmplitudeCap;
            return w;
        }
        // stroke finished → advance
        strokeCount++;
        if (strokeCount < totalStrokes)
        {
            direction *= -1;
            waveStartMs = nowMs;
            // recompute this frame in the new stroke
            float phase = 0.0f;
            int w = (int)(sinf(phase) * (float)amplitude * (float)direction);
            return w == 0 ? 0 : w;   // phase 0 → ~0
        }
        injecting = false;   // all strokes done
        return 0;
    }

    // base decoded from data2Lo/data3 (caller passes frame bytes);
    // adds human_weight + pert, writes back. data[2] high nibble is caller-kept.
    void applyToFrame(uint8_t &data2Lo, uint8_t &data3, int pert)
    {
        int torque = (int)(((uint16_t)data2Lo << 8) | data3);
        torque += (kHumanWeight + pert);
        data2Lo = (uint8_t)((torque >> 8) & 0x0F);
        data3 = (uint8_t)(torque & 0xFF);
    }
};
```

- [ ] **Step 6: 跑测试确认通过（绿）**

```bash
pio test -e native_reactive_nag
```
Expected: 7/7 PASS。若 `test_wave_half_sine_shape` 因 PRNG 抖动不稳，调 `init(seed)` 固定种子重试（种子已固定）。

- [ ] **Step 7: Commit**

```bash
git add include/dash_reactive_nag.h test/test_native_reactive_nag/ platformio.ini
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "feat(reactive-nag): DashReactiveNagBurst 状态机 + 单元测试

移植公开源码 web_dnd_steer 反应式扭矩爆发逻辑（纯状态机）：
0x399 handsOnState>=3 触发有界半正弦爆发，2~3 stroke，3 爆发后 3s 冷却，
幅度分级 60~95 + human_weight 8 + cap 95。nowMs 显式注入可测。
7 单元测试全绿。native_bionic_steer env 重命名为 native_reactive_nag。"
```

---

## Task 2: LegacyHandler 反应式集成 + 集成测试

**Files:**
- Modify: `include/handlers.h:12`（include）、`:295-377`（LegacyHandler 成员 + 0x370 块）、`:444-452`（0x399 块）
- Modify: `test/test_native_legacy/test_legacy_handler.cpp`（删 7 仿生测试 + helper refs，加反应式集成测试 + makeDasFrame）

- [ ] **Step 1: 写反应式集成测试（先红）**

Edit `test/test_native_legacy/test_legacy_handler.cpp`。

先加 0x399 frame helper（在 `decodeEchoTorqueRaw` 之后，`:44` 附近）：

```cpp
// Helper: build 0x399 (921 DAS_status) frame. handsOnState → data[5] bits[5:2].
static CanFrame makeDasFrame(uint8_t handsOnState)
{
    CanFrame f = {.id = 921, .dlc = 8};
    f.data[0] = 0x04;  // AP active (isDASAutopilotActive accepts byte0 in {3,4,5})
    f.data[1] = 0x00;
    f.data[2] = 0; f.data[3] = 0; f.data[4] = 0;
    f.data[5] = static_cast<uint8_t>((handsOnState & 0x0F) << 2);
    f.data[6] = 0; f.data[7] = 0;
    return f;
}
```

替换 7 个仿生测试（`:717` 到对应 `}` 结束，约 `:810`）为以下 5 个反应式集成测试：

```cpp
// Reactive NAG suppression — opt-in via bionicSteering, in LegacyHandler.

// toggle OFF → 无 0x370 回声（即便 NAG）
void test_legacy_reactive_off_no_echo()
{
    handler.bionicSteering = false;
    handler.handleMessage(makeDasFrame(13), mock);   // NAG
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x0C), mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// toggle ON + NAG(921 byte5=13) → 0x370 回声；扭矩 = base + human_weight(8) + wave
void test_legacy_reactive_nag_echoes_with_human_weight()
{
    handler.bionicSteering = true;
    handler.handleMessage(makeDasFrame(13), mock);                 // 触发 NAG
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x0C), mock);     // base 0x080C
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    int32_t t = decodeEchoTorqueRaw(mock.sent[0]);
    // base(0x080C=2060) + 8 + wave(首帧 phase≈0 → ≈0)；允许 ±amplitudeCap 抖动
    TEST_ASSERT_TRUE(t >= 2060 + 8 - 95);
    TEST_ASSERT_TRUE(t <= 2060 + 8 + 95);
}

// toggle ON 但无 NAG（byte5=2）→ 无回声
void test_legacy_reactive_no_nag_no_echo()
{
    handler.bionicSteering = true;
    handler.handleMessage(makeDasFrame(2), mock);                  // 非 NAG
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x0C), mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// 回声 data[4] handsOn 不被改写（保留原帧值）；counter+1；checksum 合法
void test_legacy_reactive_echo_frame_shape()
{
    handler.bionicSteering = true;
    CanFrame in = makeEpasFrame(0, 0.10, 0x0C);
    uint8_t origB4 = in.data[4];
    handler.handleMessage(makeDasFrame(13), mock);
    handler.handleMessage(in, mock);
    CanFrame e = mock.sent[0];
    TEST_ASSERT_EQUAL_UINT8(origB4, e.data[4]);                    // data[4] 不 forge
    TEST_ASSERT_EQUAL_UINT8(((0x0C + 1) & 0x0F), (e.data[6] & 0x0F)); // counter+1
    uint16_t sum = 0; for (int i = 0; i < 7; ++i) sum += e.data[i];
    TEST_ASSERT_EQUAL_UINT8((sum + 0x73) & 0xFF, e.data[7]);       // checksum
}

// toggle ON + NAG，但 checkAD 返回 false（非 AD）→ 不注入
void test_legacy_reactive_checkad_blocks()
{
    handler.bionicSteering = true;
    bool adFlag = false;
    handler.checkAD = [&adFlag]() { return adFlag; };
    handler.handleMessage(makeDasFrame(13), mock);
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x0C), mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());                         // checkAD false → 阻断
}
```

更新 `main()` 的 RUN_TEST 列表：删 7 个 `test_legacy_bionic_*`，加 5 个 `test_legacy_reactive_*`。

- [ ] **Step 2: 跑测试确认失败（红）**

```bash
pio test -e native_legacy
```
Expected: 编译失败 — `handler.bionic` 已不存在（下步才改），或测试 FAIL。

- [ ] **Step 3: 改 LegacyHandler（转绿）**

Edit `include/handlers.h`。

a) 第 12 行 include 替换：
```cpp
#include "dash_reactive_nag.h"
```
（删 `#include "dash_bionic_steer.h"`）

b) 第 295-304 行成员 + override 替换：
```cpp
struct LegacyHandler : public CarManagerBase
{
    DashReactiveNagBurst nag;  // reactive NAG-suppression burst state machine

    bool bionicDisabled() const override { return false; }  // reactive has no auto-disable
    void resetBionic(uint32_t seed) override
    {
        nag.reset();
        nag.init(seed ? seed : 0xDEADBEEF);
    }
```

c) 第 321-377 行整个 0x370 bionic 块替换为反应式回声：
```cpp
        if (frame.id == 880 && frame.dlc >= 8)
        {
            // Reactive NAG-suppression burst (opt-in via bionicSteering; default OFF).
            // Only echoes during an active burst started by 0x399 NAG detection.
            unsigned long nowMs = dashDiagNowMs();
            bool useReactive = (bool)bionicSteering && nag.shouldInject(nowMs);
            if (checkAD && !checkAD())
                useReactive = false;
            if (useReactive)
            {
                int pert = nag.computeWave(nowMs);
                CanFrame echo;
                echo.id = 880;
                echo.dlc = 8;
                echo.data[0] = frame.data[0];
                echo.data[1] = frame.data[1];
                uint8_t d2lo = frame.data[2] & 0x0F;   // Legacy 扭矩字段 base（高 nibble 保）
                uint8_t d3 = frame.data[3];
                nag.applyToFrame(d2lo, d3, pert);        // 写回 base + human_weight + wave
                echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | d2lo);
                echo.data[3] = d3;
                echo.data[4] = frame.data[4];            // 不 forge handsOn（车不读它判 NAG）
                echo.data[5] = frame.data[5];
                uint8_t cnt = static_cast<uint8_t>(frame.data[6] & 0x0F);
                cnt = static_cast<uint8_t>((cnt + 1) & 0x0F);
                echo.data[6] = static_cast<uint8_t>((frame.data[6] & 0xF0) | cnt);
                uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] +
                               echo.data[4] + echo.data[5] + echo.data[6];
                echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
                framesSent++;
                driver.send(echo);
            }
        }
```

d) 第 444-452 行 0x399 块加 NAG 采样（在 `return;` 之前）：
```cpp
        if (frame.id == 921)
        {
            if (frame.dlc < 1)
                return;
            APActive = isDASAutopilotActive(readDASAutopilotStatus(frame));
            if (frame.dlc >= 2)
                fusedSpeedLimitRaw = static_cast<uint8_t>(frame.data[1] & 0x1F);
            if (frame.dlc >= 6)
            {
                uint8_t hos = static_cast<uint8_t>((frame.data[5] >> 2) & 0x0F);
                nag.onNagSample(hos, dashDiagNowMs());
            }
            return;
        }
```

- [ ] **Step 4: 跑测试确认通过（绿）**

```bash
pio test -e native_legacy
```
Expected: 全 PASS（5 反应式 + 既有非仿生测试）。若 `test_legacy_reactive_nag_echoes_with_human_weight` 因首帧 wave 非精确 0 而边界不稳，放宽断言上限为 `base + 8 + 95 + 8`（human_weight 已含）。

- [ ] **Step 5: Commit**

```bash
git add include/handlers.h test/test_native_legacy/test_legacy_handler.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "feat(legacy): LegacyHandler 反应式扭矩爆发集成

0x399 byte5 bits[5:2] 采样喂 DashReactiveNagBurst；0x370 块从持续仿生改为
爆发期回声（base 从帧读 + human_weight + wave，写 data[2:3]，保高 nibble，
不 forge data[4] handsOn）。删 7 仿生测试，加 5 反应式集成测试。"
```

---

## Task 3: 删除 DashBionicSteer + 清理

**Files:**
- Delete: `include/dash_bionic_steer.h`
- Modify: `include/web/mcp2515_dashboard.h:6847`（stale 注释）

- [ ] **Step 1: 确认 DashBionicSteer 无残留引用**

```bash
grep -rn "DashBionicSteer\|dash_bionic_steer\|\.bionic\b" include/ src/ test/
```
Expected: 仅 `dashboard.h:6847` 的注释提及（无代码引用）。若有代码残留，先清理。

- [ ] **Step 2: 删除旧头文件**

```bash
rm include/dash_bionic_steer.h
```

- [ ] **Step 3: 更新 stale 注释**

`include/web/mcp2515_dashboard.h:6847`，把
```cpp
    // DashBionicSteer is retained (standalone-tested) but currently unused in production.
```
改为
```cpp
    // LegacyHandler uses DashReactiveNagBurst (dash_reactive_nag.h) for reactive NAG suppression.
```

- [ ] **Step 4: 全量 native 构建验证**

```bash
pio test -e native_legacy -e native_reactive_nag -e native_nag
```
Expected: 全 PASS（DashBionicSteer 已无引用，编译干净）。

- [ ] **Step 5: Commit**

```bash
git add include/dash_bionic_steer.h include/web/mcp2515_dashboard.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "chore: 删除无效的 DashBionicSteer，清理 stale 注释

持续仿生实测对 NAG 无效（nag 源=0x399 byte5，非 0x370 data[4]），
已被 DashReactiveNagBurst 取代。"
```

---

## Task 4: 契约测试 — 解除 0x370 禁令 + 反应式安全契约

**Files:**
- Modify: `test/test_no_epas_nag_contract.py`

- [ ] **Step 1: 删两个过时契约测试**

Edit `test/test_no_epas_nag_contract.py`：删除函数 `test_no_epas_nag_symbols_in_compiled_source`（第 47-54 行整段）和 `test_legacy_bionic_steering_is_optin_and_gated`（第 98-118 行整段）。保留 `test_no_native_epas_nag_env`、`test_nag_torque_tamper_*` 三项。

- [ ] **Step 2: 加反应式安全契约测试**

在文件末尾追加：

```python
def test_reactive_nag_is_optin_and_bounded():
    """DashReactiveNagBurst (0x370 reactive torque burst) must be opt-in and bounded:
    bionicSteering defaults false, the burst is gated, amplitude is capped, and
    the burst is bounded (max bursts + cooldown). Replaces the lifted 0x370 ban
    (Jordan 2026-06-25 authorized lifting the ban + ignoring the Mode-C incident)."""
    h = (ROOT / "include" / "handlers.h").read_text()
    rn = (ROOT / "include" / "dash_reactive_nag.h").read_text()

    # bionicSteering member defaults false (on CarManagerBase)
    assert re.search(r"Shared<bool>\s+bionicSteering\s*\{[^}]*\bfalse\b", h), \
        "bionicSteering must default to false"

    # LegacyHandler 0x370 echo gated on bionicSteering + nag.shouldInject
    assert "bool useReactive = (bool)bionicSteering && nag.shouldInject(nowMs)" in h, \
        "reactive echo must be gated on bionicSteering + nag.shouldInject"

    # 0x399 NAG detection reads byte5 bits[5:2] (read-only, no forge)
    assert "(frame.data[5] >> 2) & 0x0F" in h, "0x399 handsOn decode must read byte5 bits[5:2]"

    # DashReactiveNagBurst exists with bounded burst + amplitude cap
    assert "DashReactiveNagBurst" in rn
    assert "kMaxBursts{3}" in rn, "burst must be bounded (max 3)"
    assert "kCooldownMs{3000}" in rn, "cooldown must be enforced (3 s)"
    assert "kAmplitudeCap{95}" in rn, "amplitude hard cap must exist"
    assert "kHumanWeight{8}" in rn
```

- [ ] **Step 3: 跑契约测试确认通过**

```bash
python3 -m pytest test/test_no_epas_nag_contract.py -q
```
Expected: PASS（删 2 + 加 1 = 净增测试通过；保留 3 项 nag_tamper 测试不受影响，因 NagHandler opt-in 路径未动）。

- [ ] **Step 4: Commit**

```bash
git add test/test_no_epas_nag_contract.py
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "test(contract): 解除 0x370 禁令，换反应式安全契约

Jordan 授权解禁。删 8 符号一刀切禁令 + 仿生契约；加反应式安全契约
（默认 OFF / shouldInject 门控 / byte5 只读 / 爆发有界 kMaxBursts=3 +
kCooldownMs=3000 / 幅度 cap 95）。保留 NagHandler opt-in tamper 契约。"
```

---

## Task 5: 诊断 — 串口命令 + /status + UI 文案

**Files:**
- Modify: `include/handlers.h`（CarManagerBase 加 virtual reactive 诊断）
- Modify: `include/web/mcp2515_dashboard.h`（串口命令 + /status JSON）
- Modify: `include/web/mcp2515_dashboard_ui.src.h`（toggle + chip 文案）

- [ ] **Step 1: CarManagerBase 加 reactive 诊断 virtual**

`include/handlers.h` 第 290-291 行（bionicDisabled/resetBionic 旁）加：
```cpp
    virtual bool bionicDisabled() const { return false; }
    virtual void resetBionic(uint32_t seed) { (void)seed; }
    // Append reactive-NAG diag JSON fields (empty by default; LegacyHandler overrides).
    virtual void appendReactiveDiagJson(String & /*out*/) const {}
    virtual ~CarManagerBase() = default;
```

LegacyHandler 加 override（`resetBionic` 之后，Task 2 改过的成员区）：
```cpp
    void appendReactiveDiagJson(String &out) const override
    {
        out += "\"enabled\":";
        out += (bionicSteering ? "true" : "false");
        out += ",\"nagActive\":";
        out += (nag.isNagActive() ? "true" : "false");
        out += ",\"injecting\":";
        out += (nag.shouldInject(0) ? "true" : "false");
        out += ",\"burstsThisCycle\":";
        out += String(nag.burstsThisCycle());
        out += ",\"lastAmplitude\":";
        out += String(nag.lastAmplitude());
        out += ",\"lastHandsOnState\":";
        out += String(nag.lastHandsOnState);
    }
```

- [ ] **Step 2: /status 注入 reactive 字段**

`include/web/mcp2515_dashboard.h` 找到 /status JSON 拼接（`dashBionicSteering` 读取处，`:3120` 附近），在该行后加：
```cpp
    j += "\"reactiveNag\":{";
    if (dashHandler) dashHandler->appendReactiveDiagJson(j);
    else j += "\"enabled\":false";
    j += "},";
```

- [ ] **Step 3: 串口命令 `reactive_nag`**

找到串口命令分发处（grep 既有 `cmd == "xxx"` 模式；**用新名 reactive_nag**，非旧禁符号）。`dashHandler` 是 `CarManagerBase*`，`nag` 成员不可直达，故复用 `appendReactiveDiagJson`：
```cpp
    if (cmd == "reactive_nag" && dashHandler)
    {
        Serial.println(F("=== Reactive NAG ==="));
        String j;
        dashHandler->appendReactiveDiagJson(j);
        Serial.println(j);
    }
```

- [ ] **Step 4: UI toggle + chip 文案**

`include/web/mcp2515_dashboard_ui.src.h` 找到 `def-bionic-tgl`（grep），把可见文案「仿生方向盘」/描述改为「NAG 抑制（反应式扭矩）」/「检测到握方向盘警告时，反应式爆发扭矩抑制 NAG。实车验证中，故障即关」。找到 `def-bionic-risk` chip，文案改为「反应式扭矩注入 · 实车验证中 / 故障立即关闭」。

- [ ] **Step 5: 构建 + native 验证**

```bash
pio test -e native_legacy -e native_reactive_nag
pio run -e waveshare_single_can_standalone
```
Expected: native 全绿；ESP 构建 SUCCESS（appendReactiveDiagJson 用 String，ESP 端可用；native_legacy 不含 String 时该 virtual 默认空实现兜底——若 native 编译报 String 未定义，用 `#ifdef ESP_PLATFORM` 或 `ARDUINO` 包住 override 的 String 引用，native 走基类空实现）。

- [ ] **Step 6: Commit**

```bash
git add include/handlers.h include/web/mcp2515_dashboard.h include/web/mcp2515_dashboard_ui.src.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "feat(diag): 反应式 NAG 诊断 + UI 文案

CarManagerBase 加 appendReactiveDiagJson virtual；LegacyHandler 暴露
enabled/nagActive/injecting/bursts/lastAmplitude/handsOn。/status 加 reactiveNag
对象，串口 reactive_nag 命令，UI toggle/chip 文案改 NAG 抑制。"
```

---

## Task 6: 全量验证 + 资产构建

- [ ] **Step 1: 全 native 测试矩阵**

```bash
pio test -e native_legacy -e native_reactive_nag -e native_nag -e native_dashboard -e native_helpers
```
Expected: 全 PASS。

- [ ] **Step 2: Python 契约 + 既有 pytest**

```bash
python3 -m pytest test/ -q
```
Expected: 全 PASS（含新的反应式契约）。

- [ ] **Step 3: ESP 生产构建（Legacy profile）**

确认 `platformio_profile.h` 为 `#define LEGACY`（Jordan 车是 Legacy）。然后：
```bash
pio run -e waveshare_single_can_standalone
```
Expected: SUCCESS。验证 version 戳含新 commit（若 .pio 缓存陈旧，`pio run -t clean` 后重建）。

- [ ] **Step 4: clang-format（CI lint）**

```bash
pio run -e waveshare_single_can_standalone -t exec -- clang-format --dry-run -Werror include/dash_reactive_nag.h include/handlers.h 2>/dev/null || \
  clang-format -i include/dash_reactive_nag.h
```
（若项目有 .clang-format；按 CI 实际命令执行。）

- [ ] **Step 5: 最终 commit（若有格式修正）**

```bash
git add -A
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "chore: clang-format + 全量验证绿（反应式 NAG 抑制 Legacy 移植完成）" || echo "无改动"
```

---

## 自检（实施完成后 controller 跑）

对照 spec 每节：
- §5 架构 → Task 1-3 ✓
- §6 DashReactiveNagBurst → Task 1 ✓
- §7 LegacyHandler 集成 → Task 2 ✓
- §8 安全护栏 → Task 1（cap/有界/默认OFF）+ Task 4（契约）✓
- §9 诊断 → Task 5 ✓
- §10 UI/toggle → Task 5 ✓
- §11 契约变更 → Task 4 ✓
- §12 测试矩阵 → Task 1（单元）+ Task 2（集成）+ Task 6（全量）✓
- §13 验证流程 → Task 6 桌面+构建；台架/实车待 Jordan ✓

**台架 + 实车（Task 6 之后，Jordan 执行）**：烧板 → 回放 CSV（含 NAG 段）验证检测/爆发/冷却 → 安全区实车（手扶盘，故障即关 toggle + 拔模块）→ 录 reactive_nag 分析。
