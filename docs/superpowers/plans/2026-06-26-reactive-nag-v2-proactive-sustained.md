# 反应式 NAG 抑制 v2（Proactive + 持续扭矩）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 v1 反应式 NAG 抑制（零均值正弦，部分有效）升级为 v2 三模式（IDLE/PROACTIVE/REACTIVE）+ 持续 DC 偏置扭矩 + 删冷却误时 + data[4] forge，目标「大部分清除」NAG。

**Architecture:** 重写 `DashReactiveNagBurst`（dash_reactive_nag.h）为三模式状态机：PROACTIVE（hos≤2 周期轻持续 hold 预防触发）+ REACTIVE（hos≥3 持续 DC 偏置 hold，积分>0，修 v1 零均值不清）+ IDLE。删 v1 的 3-burst-3s-cooldown（rec8 bug 源），改 NAG 期 800ms 间隔 + NAG 清除全复位。所有回声 forge data[4] handsOnLevel=1（C）。诊断加 mode/proactiveWiggles/reactiveBursts + NVS 持久化。

**Tech Stack:** ESP-IDF + PlatformIO（C++17），Unity native 测试，Python pytest 契约，TWAI CAN。

**Spec:** `docs/superpowers/specs/2026-06-26-reactive-nag-v2-proactive-sustained-design.md`

**Git:** 每任务一 commit，`git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit`，本地 main，不 push。

**关键事实（写代码前必读）:**
- 0x370 扭矩字段 = `(data[2]&0x0F)<<8 | data[3]`，data[2] 高 nibble 必须保。校验 `(Σdata[0..6]+0x73)&0xFF`→data[7]。counter `(data[6]&0x0F+1)&0x0F`→data[6] 低 nibble（保高）。
- NAG 源 = 0x399 byte5 bits[5:2]（hos），≥3 警告。只读。
- v1 失效模式：rec8（冷却误时跨 episode）+ rec9（零均值正弦积分≈0，DAS 看不见）。v2 对策：持续 DC 偏置 + 清除全复位。
- `dashDiagNowMs()` native 是每调用+1 计数器；时间方法显式接 `nowMs`，单元测试注入精确值。

---

## 文件结构

| 文件 | 责任 | 动作 |
|---|---|---|
| `include/dash_reactive_nag.h` | DashReactiveNagBurst v2 三模式 + 持续 hold + DashReactiveDiag | **重写** |
| `include/handlers.h` | LegacyHandler 0x370 块：shouldEcho + computeHold + data[4] forge + active 参 | 修改 |
| `include/web/mcp2515_dashboard.h` | reactive_nag + /status v2 字段；NVS flush 2s | 修改 |
| `test/test_native_reactive_nag/test_main.cpp` | v2 单元测试（重写） | **重写** |
| `test/test_native_legacy/test_legacy_handler.cpp` | 集成测试适配 v2 | 修改 |
| `test/test_no_epas_nag_contract.py` | 反应式契约 v2 | 修改 |

---

## Task 1: 重写 DashReactiveNagBurst v2 引擎 + 单元测试

**Files:**
- Rewrite: `include/dash_reactive_nag.h`
- Rewrite: `test/test_native_reactive_nag/test_main.cpp`

- [ ] **Step 1: 写 v2 单元测试（先红）**

Rewrite `test/test_native_reactive_nag/test_main.cpp` 全文：

```cpp
#include <unity.h>
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

// 1. 非激活（active=false）→ 不注入，模式跟随 hos 但不启动 burst
void test_inactive_no_burst()
{
    DashReactiveNagBurst n;
    n.init(12345);
    n.onNagSample(13, 100, false);  // NAG but inactive
    TEST_ASSERT_EQUAL(NagMode::REACTIVE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL(0, n.reactiveBursts());
}

// 2. REACTIVE：active + hos>=3 → 立即 hold，模式 REACTIVE
void test_reactive_starts_hold()
{
    DashReactiveNagBurst n;
    n.init(1);
    n.onNagSample(5, 100, true);
    TEST_ASSERT_EQUAL(NagMode::REACTIVE, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL(1, n.reactiveBursts());
}

// 3. 持续几何（核心，区别 v1）：REACTIVE 多帧 computeHold 全部 > 0（积分>0）
void test_reactive_hold_positive_integral()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(5, 0, true);
    int sum = 0, mn = 1 << 30;
    for (unsigned long t = 0; t < 1200; t += 20)  // 1.2s, ~50Hz
    {
        if (!n.shouldEcho(t)) break;
        int p = n.computeHold(t);
        if (p < mn) mn = p;
        sum += p;
    }
    TEST_ASSERT_TRUE(mn > 0);   // 全正（持续 hold，非零均值）
    TEST_ASSERT_TRUE(sum > 0);  // 积分 > 0
}

// 4. PROACTIVE：active + hos<=2 → 周期性轻 hold
void test_proactive_periodic_hold()
{
    DashReactiveNagBurst n;
    n.init(3);
    n.onNagSample(0, 100, true);   // 无 NAG, active
    TEST_ASSERT_EQUAL(NagMode::PROACTIVE, n.mode());
    // 首次进入 PROACTIVE 立即排一次（nextProactiveMs=now）
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL(1, n.proactiveWiggles());
}

// 5. NAG 清除全复位（修 rec8）：hos>=3 → <=2 → reactive 状态清，下段 NAG 立即爆发
void test_nag_clear_resets_no_stale_cooldown()
{
    DashReactiveNagBurst n;
    n.init(4);
    n.onNagSample(5, 100, true);   // burst 1
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    while (n.shouldEcho(200)) n.computeHold(200);  // drain
    n.onNagSample(0, 300, true);   // NAG clear → full reset
    TEST_ASSERT_EQUAL(NagMode::PROACTIVE, n.mode());
    // 紧接新 NAG（无 3s 冷却挡）
    n.onNagSample(5, 400, true);
    TEST_ASSERT_TRUE(n.shouldEcho(400));   // 立即爆发，无陈旧冷却
    TEST_ASSERT_EQUAL(2, n.reactiveBursts());
}

// 6. NAG 期内 ~800ms 间隔（不超密）
void test_reactive_intra_episode_gap()
{
    DashReactiveNagBurst n;
    n.init(5);
    n.onNagSample(5, 100, true);   // burst 1
    while (n.shouldEcho(200)) n.computeHold(200);  // drain (ends, lastReactiveEndMs=200)
    n.onNagSample(5, 500, true);   // gap 300ms < 800 → no new burst
    TEST_ASSERT_FALSE(n.shouldEcho(500));
    n.onNagSample(5, 1100, true);  // gap 900ms > 800 → burst 2
    TEST_ASSERT_TRUE(n.shouldEcho(1100));
    TEST_ASSERT_EQUAL(2, n.reactiveBursts());
}

// 7. 模式切换：proactive 进行中 NAG 起 → 切 REACTIVE
void test_proactive_interrupted_by_nag()
{
    DashReactiveNagBurst n;
    n.init(6);
    n.onNagSample(0, 100, true);   // proactive
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    n.onNagSample(5, 120, true);   // NAG → REACTIVE (interrupts proactive)
    TEST_ASSERT_EQUAL(NagMode::REACTIVE, n.mode());
}

// 8. data[4] forge 由 handler 负责（引擎 applyToFrame 只改扭矩）；这里测 applyToFrame
void test_apply_to_frame_adds_human_weight_and_pert()
{
    DashReactiveNagBurst n;
    n.init(7);
    uint8_t d2lo = 0x08, d3 = 0x12;   // base 0x0812
    n.applyToFrame(d2lo, d3, 70);     // +human_weight 8 + pert 70 = +78
    int out = (int)(((uint16_t)d2lo << 8) | d3);
    TEST_ASSERT_EQUAL_INT16(0x0812 + 8 + 70, out);
}

// 9. 峰值 cap：computeHold 不超 kAmplitudeCap
void test_hold_respects_cap()
{
    DashReactiveNagBurst n;
    n.init(8);
    n.onNagSample(5, 0, true);
    for (unsigned long t = 0; t < 1200; t += 20)
    {
        if (!n.shouldEcho(t)) break;
        int p = n.computeHold(t);
        TEST_ASSERT_TRUE(p <= n.amplitudeCap());
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_inactive_no_burst);
    RUN_TEST(test_reactive_starts_hold);
    RUN_TEST(test_reactive_hold_positive_integral);
    RUN_TEST(test_proactive_periodic_hold);
    RUN_TEST(test_nag_clear_resets_no_stale_cooldown);
    RUN_TEST(test_reactive_intra_episode_gap);
    RUN_TEST(test_proactive_interrupted_by_nag);
    RUN_TEST(test_apply_to_frame_adds_human_weight_and_pert);
    RUN_TEST(test_hold_respects_cap);
    return UNITY_END();
}
```

- [ ] **Step 2: 跑测试确认失败（红）**

```bash
pio test -e native_reactive_nag
```
Expected: 编译失败（v1 引擎 API 不匹配：无 mode()、NagMode、onNagSample 三参、computeHold 等）。

- [ ] **Step 3: 重写 DashReactiveNagBurst v2（转绿）**

Rewrite `include/dash_reactive_nag.h` 全文：

```cpp
#pragma once

// dash_reactive_nag.h — Reactive NAG-suppression v2: 3-mode (IDLE/PROACTIVE/REACTIVE)
// + sustained DC-biased torque hold (integral > 0, vs v1 zero-mean sine).
// Port of public-source web_dnd_steer, Legacy-adapted + v2 optimizations (A+B+C).
//
// Modes (when active = toggle ON + APActive):
//   PROACTIVE  hos<=2 → periodic gentle sustained hold (every 2-5 s) to prevent NAG trigger
//   REACTIVE   hos>=3 → sustained strong hold (DC bias + jitter), fires at hos=3
//   IDLE       inactive
// NAG clear (hos<=2) fully resets reactive state + throttle (fixes v1 rec8 stale-cooldown).
//
// Pure logic: all time passed explicitly as nowMs (testable).

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
    uint32_t range(uint32_t lo, uint32_t hi) { return (hi < lo) ? lo : lo + (next() % (hi - lo + 1)); }
};

enum class NagMode { IDLE, PROACTIVE, REACTIVE };

// POD snapshot for /status + serial diag. No String (native-compilable).
struct DashReactiveDiag
{
    bool enabled{false};
    NagMode mode{NagMode::IDLE};
    bool injecting{false};
    uint8_t lastHandsOnState{0};
    int currentAmp{0};
    uint32_t nagSamples{0};
    uint32_t reactiveBursts{0};
    uint32_t proactiveWiggles{0};
    uint32_t echoSent{0};
    unsigned long nextProactiveInMs{0};
};

struct DashReactiveNagBurst
{
    // tunables
    static constexpr int kHumanWeight{8};        // baseline hands-present offset (added in applyToFrame)
    static constexpr int kNagThreshold{3};
    static constexpr int kReactiveAmp{70};        // REACTIVE DC bias magnitude
    static constexpr int kReactiveJitter{15};     // ± sine jitter on REACTIVE hold
    static constexpr int kProactiveAmp{35};       // PROACTIVE gentle DC bias
    static constexpr int kProactiveJitter{12};
    static constexpr int kAmplitudeCap{95};       // hard cap on |perturbation|
    static constexpr int kStrokesMin{2}, kStrokesMax{3};
    static constexpr int kStrokeDurLo{300}, kStrokeDurHi{400};   // ms per stroke
    static constexpr unsigned long kReactiveGapMs{800};          // intra-episode REACTIVE burst gap
    static constexpr unsigned long kProactiveIntLo{2000}, kProactiveIntHi{5000}; // PROACTIVE interval

    // runtime state
    DashReactivePRNG rng;
    NagMode mode_{NagMode::IDLE};
    bool injecting{false};
    bool proactiveBurst_{false};   // current burst is proactive (gentle) vs reactive (strong)
    unsigned long waveStartMs{0};
    int strokeCount{0};
    int totalStrokes{2};
    int strokeDurMs{350};
    int amp_{0};                    // DC bias magnitude for current burst
    int jitter_{0};                 // ± sine jitter amplitude for current burst
    unsigned long lastReactiveEndMs{0};
    unsigned long nextProactiveMs{0};
    uint8_t lastHandsOnState{0};
    bool nagActive_{false};
    // instrumentation
    uint32_t nagSamples_{0};
    uint32_t reactiveBursts_{0};
    uint32_t proactiveWiggles_{0};
    uint32_t echoSent_{0};

    void init(uint32_t seed) { rng.seed(seed); nextProactiveMs = 0; }
    void reset()
    {
        mode_ = NagMode::IDLE;
        injecting = false;
        proactiveBurst_ = false;
        strokeCount = 0;
        lastReactiveEndMs = 0;
        nextProactiveMs = 0;
        lastHandsOnState = 0;
        nagActive_ = false;
    }
    void resetCounters() { nagSamples_ = 0; reactiveBursts_ = 0; proactiveWiggles_ = 0; echoSent_ = 0; }
    void setCounters(uint32_t ns, uint32_t rb, uint32_t pw, uint32_t es)
    {
        nagSamples_ = ns;
        reactiveBursts_ = rb;
        proactiveWiggles_ = pw;
        echoSent_ = es;
    }
    void notifyEchoSent() { echoSent_++; }

    bool isNagActive() const { return nagActive_; }
    NagMode mode() const { return mode_; }
    bool shouldEcho(unsigned long /*nowMs*/) const { return injecting; }
    int amplitudeCap() const { return kAmplitudeCap; }
    int currentAmp() const { return injecting ? amp_ : 0; }
    uint32_t reactiveBursts() const { return reactiveBursts_; }
    uint32_t proactiveWiggles() const { return proactiveWiggles_; }
    unsigned long nextProactiveInMs(unsigned long nowMs) const
    {
        return (mode_ == NagMode::PROACTIVE && nextProactiveMs > nowMs) ? (nextProactiveMs - nowMs) : 0;
    }

    void startBurst(bool proactive, unsigned long nowMs)
    {
        injecting = true;
        proactiveBurst_ = proactive;
        strokeCount = 0;
        waveStartMs = nowMs;
        totalStrokes = (int)rng.range(kStrokesMin, kStrokesMax);
        strokeDurMs = (int)rng.range(kStrokeDurLo, kStrokeDurHi);
        if (proactive)
        {
            amp_ = kProactiveAmp;
            jitter_ = kProactiveJitter;
            proactiveWiggles_++;
        }
        else
        {
            amp_ = kReactiveAmp;
            jitter_ = kReactiveJitter;
            reactiveBursts_++;
        }
    }

    // Called per 0x399. hos = (0x399 data[5]>>2)&0x0F. active = toggle ON && APActive.
    void onNagSample(uint8_t hos, unsigned long nowMs, bool active)
    {
        if (hos != lastHandsOnState)
        {
            lastHandsOnState = hos;
            if (hos <= 2)
            {
                // NAG cleared → full reset of reactive state + throttle (fixes v1 rec8)
                nagActive_ = false;
                if (mode_ == NagMode::REACTIVE)
                {
                    injecting = false;
                    lastReactiveEndMs = 0;
                    mode_ = NagMode::PROACTIVE;
                    nextProactiveMs = nowMs;   // schedule proactive soon
                }
            }
        }
        if (hos >= kNagThreshold)
        {
            nagActive_ = true;
            nagSamples_++;
            mode_ = NagMode::REACTIVE;
            if (!active) return;
            if (injecting && proactiveBurst_)
                injecting = false;            // preempt ongoing proactive wiggle with reactive
            if (injecting) return;            // reactive hold ongoing
            bool gapOk = (lastReactiveEndMs == 0) || ((nowMs - lastReactiveEndMs) > kReactiveGapMs);
            if (gapOk) startBurst(false, nowMs);
        }
        else
        {
            nagActive_ = false;
            if (mode_ == NagMode::REACTIVE) return;  // wait for the hos-change block to transition
            mode_ = NagMode::PROACTIVE;
            if (!active) return;
            if (nextProactiveMs == 0) nextProactiveMs = nowMs;
            if (!injecting && nowMs >= nextProactiveMs) startBurst(true, nowMs);
        }
    }

    // DC bias + small sine jitter. Same-sign → integral > 0 (sustained hold).
    int computeHold(unsigned long nowMs)
    {
        if (!injecting) return 0;
        unsigned long elapsed = nowMs - waveStartMs;
        if (elapsed <= (unsigned long)strokeDurMs)
        {
            float phase = ((float)elapsed / (float)strokeDurMs) * (float)M_PI;
            int pert = amp_ + (int)(sinf(phase) * (float)jitter_);
            if (pert > kAmplitudeCap) pert = kAmplitudeCap;
            if (pert < 0) pert = 0;            // keep same-sign (sustained)
            return pert;
        }
        strokeCount++;
        if (strokeCount < totalStrokes)
        {
            waveStartMs = nowMs;
            int pert = amp_ > kAmplitudeCap ? kAmplitudeCap : amp_;
            return pert;
        }
        injecting = false;
        if (proactiveBurst_) nextProactiveMs = nowMs + rng.range(kProactiveIntLo, kProactiveIntHi);
        else lastReactiveEndMs = nowMs;
        return 0;
    }

    // base decoded from data2Lo/data3; adds human_weight + pert. data[2] high nibble caller-kept.
    void applyToFrame(uint8_t &data2Lo, uint8_t &data3, int pert)
    {
        int torque = (int)(((uint16_t)data2Lo << 8) | data3);
        torque += (kHumanWeight + pert);
        data2Lo = (uint8_t)((torque >> 8) & 0x0F);
        data3 = (uint8_t)(torque & 0xFF);
    }

    DashReactiveDiag diag(unsigned long nowMs) const
    {
        return {false, mode_,  injecting,        lastHandsOnState, currentAmp(),
                nagSamples_, reactiveBursts_, proactiveWiggles_, echoSent_,
                nextProactiveInMs(nowMs)};
    }
};
```

- [ ] **Step 4: 跑测试确认通过（绿）**

```bash
pio test -e native_reactive_nag
```
Expected: 9/9 PASS。若 `test_proactive_periodic_hold` 因 PRNG 种子导致 nextProactiveMs 排程差异不稳，调 `init(seed)` 固定种子重试（种子已固定）。

- [ ] **Step 5: Commit**

```bash
git add include/dash_reactive_nag.h test/test_native_reactive_nag/test_main.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "feat(reactive-nag-v2): 三模式引擎 + 持续 DC 偏置扭矩（重写）

DashReactiveNagBurst v2：IDLE/PROACTIVE/REACTIVE 三模式。
REACTIVE 改持续 DC 偏置 hold（积分>0，修 v1 rec9 零均值不清）；删 3s 冷却
改 800ms 间隔 + NAG 清除全复位（修 rec8 跨episode 残留）；PROACTIVE 周期轻
hold。onNagSample 加 active 参。9 单元测试（含持续几何积分>0、清除复位、
800ms 间隔）。"
```

---

## Task 2: LegacyHandler 0x370 v2 集成 + data[4] forge（C）

**Files:**
- Modify: `include/handlers.h`（0x370 块 + 0x399 块 + diag/counters override）
- Modify: `test/test_native_legacy/test_legacy_handler.cpp`（适配 v2）

- [ ] **Step 1: 改 LegacyHandler 0x370 块（v2 + data[4] forge）**

`include/handlers.h` 找到 0x370 反应式块（`if (frame.id == 880 && frame.dlc >= 8)`，~line 328）。把 v1 的 `shouldInject`/`computeWave` 改为 v2 的 `shouldEcho`/`computeHold`，并加 data[4] forge：

```cpp
        if (frame.id == 880 && frame.dlc >= 8)
        {
            // Reactive NAG-suppression v2 (opt-in via bionicSteering; default OFF).
            unsigned long nowMs = dashDiagNowMs();
            bool active = (bool)bionicSteering && APActive;
            bool useReactive = active && nag.shouldEcho(nowMs);
            if (checkAD && !checkAD())
                useReactive = false;
            if (useReactive)
            {
                int pert = nag.computeHold(nowMs);
                CanFrame echo;
                echo.id = 880;
                echo.dlc = 8;
                echo.data[0] = frame.data[0];
                echo.data[1] = frame.data[1];
                uint8_t d2lo = frame.data[2] & 0x0F;
                uint8_t d3 = frame.data[3];
                nag.applyToFrame(d2lo, d3, pert);
                echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | d2lo);
                echo.data[3] = d3;
                echo.data[4] = static_cast<uint8_t>((frame.data[4] & 0x3F) | 0x40); // C: handsOnLevel=1
                echo.data[5] = frame.data[5];
                uint8_t cnt = static_cast<uint8_t>(frame.data[6] & 0x0F);
                cnt = static_cast<uint8_t>((cnt + 1) & 0x0F);
                echo.data[6] = static_cast<uint8_t>((frame.data[6] & 0xF0) | cnt);
                uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] +
                               echo.data[4] + echo.data[5] + echo.data[6];
                echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
                framesSent++;
                driver.send(echo);
                nag.notifyEchoSent();
            }
        }
```

- [ ] **Step 2: 改 0x399 块传 active 参**

`include/handlers.h` 找到 0x399 块（`if (frame.id == 921)`，~line 427）。把 onNagSample 调用改为三参：

```cpp
            if (frame.dlc >= 6)
            {
                uint8_t hos = static_cast<uint8_t>((frame.data[5] >> 2) & 0x0F);
                bool active = (bool)bionicSteering && APActive;
                nag.onNagSample(hos, dashDiagNowMs(), active);
            }
```

- [ ] **Step 3: reactiveDiag override 填 enabled**

LegacyHandler 的 `reactiveDiag()` override（~line 310）填 enabled：

```cpp
    DashReactiveDiag reactiveDiag() const override
    {
        DashReactiveDiag d = nag.diag(dashDiagNowMs());
        d.enabled = (bool)bionicSteering;
        return d;
    }
```

- [ ] **Step 4: 适配集成测试**

`test/test_native_legacy/test_legacy_handler.cpp`：v1 的 5 反应式集成测适配 v2 API（onNagSample 三参 / shouldEcho / computeHold）。关键测：

```cpp
// toggle ON + NAG(921 byte5=13) → 0x370 回声，扭矩 = base + human_weight + 持续hold(>0)
void test_legacy_reactive_v2_nag_echoes_sustained()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrame(13);
    handler.handleMessage(das, mock);                 // APActive=true (byte0=4)
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C);        // base 0x080C=2060
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    CanFrame e = mock.sent[0];
    int32_t t = decodeEchoTorqueRaw(e);
    TEST_ASSERT_TRUE(t >= 2060 + 8);                  // base + human_weight + (hold>0)
    TEST_ASSERT_TRUE((e.data[4] & 0xC0) == 0x40);     // C: handsOnLevel=1 forged
    uint8_t cnt = (e.data[6] & 0x0F);
    TEST_ASSERT_EQUAL_UINT8(((0x0C + 1) & 0x0F), cnt); // counter+1
    uint16_t sum = 0; for (int i = 0; i < 7; ++i) sum += e.data[i];
    TEST_ASSERT_EQUAL_UINT8((sum + 0x73) & 0xFF, e.data[7]); // checksum
}
```
（保留 v1 的 off→no-echo / no-NAG→no-echo / checkAD-blocks 测，适配 v2 API。删 v1 的 `nag_echoes_with_human_weight` 紧边界测，换上面的 sustained 测。）

- [ ] **Step 5: 跑 + 提交**

```bash
pio test -e native_legacy 2>&1 | tail -8
```
Expected: 全 PASS。

```bash
git add include/handlers.h test/test_native_legacy/test_legacy_handler.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "feat(legacy-v2): 0x370 反应式 v2 集成 + data[4] handsOn forge

shouldEcho + computeHold（持续 DC 偏置）；data[4] bits[7:6]=01 forge（C，扭矩+
握手组合）；onNagSample 传 active 参；reactiveDiag 填 enabled。集成测验 sustained
扭矩(>base+8) + data[4] forge + counter/checksum。"
```

---

## Task 3: 诊断 v2（mode/proactiveWiggles/reactiveBursts）+ NVS flush 2s

**Files:**
- Modify: `include/web/mcp2515_dashboard.h`（reactive_nag 串口 + /status JSON + NVS flush）

- [ ] **Step 1: reactive_nag 串口加 v2 字段**

找到 `reactive_nag` 命令块（~line 5432）。把 printf 改为 v2 字段：

```cpp
            Serial.println("=== Reactive NAG v2 ===");
            Serial.printf("enabled=%d mode=%d injecting=%d amp=%d handsOn=%d nextProactiveMs=%lu\n",
                          (int)d.enabled, (int)d.mode, (int)d.injecting, d.currentAmp,
                          d.lastHandsOnState, (unsigned long)d.nextProactiveInMs);
            Serial.printf("nagSamples=%lu reactiveBursts=%lu proactiveWiggles=%lu echoSent=%lu\n",
                          (unsigned long)d.nagSamples, (unsigned long)d.reactiveBursts,
                          (unsigned long)d.proactiveWiggles, (unsigned long)d.echoSent);
```

- [ ] **Step 2: /status reactiveNag JSON 加 v2 字段**

找到 /status `reactiveNag` 对象（~line 2425）。把字段改为 v2：

```cpp
        j += "\"enabled\":";
        j += d.enabled ? "true" : "false";
        j += ",\"mode\":";
        j += String((int)d.mode);
        j += ",\"injecting\":";
        j += d.injecting ? "true" : "false";
        j += ",\"currentAmp\":";
        j += String(d.currentAmp);
        j += ",\"lastHandsOnState\":";
        j += String(d.lastHandsOnState);
        j += ",\"nextProactiveInMs\":";
        j += String(d.nextProactiveInMs);
        j += ",\"nagSamples\":";
        j += String(d.nagSamples);
        j += ",\"reactiveBursts\":";
        j += String(d.reactiveBursts);
        j += ",\"proactiveWiggles\":";
        j += String(d.proactiveWiggles);
        j += ",\"echoSent\":";
        j += String(d.echoSent);
```

- [ ] **Step 3: NVS flush 改 2s + keys 适配 v2**

找到 `dashReactiveCountersMaintenance`（~line 7035）。把 flush 间隔 5000→2000，keys 改 v2（rn_ns/rn_rb/rn_pw/rn_es），boot-load + reset 同步：

```cpp
static void dashReactiveCountersMaintenance()
{
    if (!dashHandler)
        return;
    unsigned long now = millis();
    if (!reactiveCountersLoaded)
    {
        dashHandler->setReactiveCounters(prefs.getUInt("rn_ns", 0), prefs.getUInt("rn_rb", 0),
                                         prefs.getUInt("rn_pw", 0), prefs.getUInt("rn_es", 0));
        reactiveCountersLoaded = true;
        lastReactiveCountersMs = now;
        return;
    }
    if (now - lastReactiveCountersMs < 2000)
        return;
    lastReactiveCountersMs = now;
    DashReactiveDiag d = dashHandler->reactiveDiag();
    prefs.putUInt("rn_ns", d.nagSamples);
    prefs.putUInt("rn_rb", d.reactiveBursts);
    prefs.putUInt("rn_pw", d.proactiveWiggles);
    prefs.putUInt("rn_es", d.echoSent);
}
```

`reactive_nag_reset` 命令（~line 5462）的 keys 改 v2：

```cpp
    else if (strcmp(start, "reactive_nag_reset") == 0)
    {
        if (dashHandler)
            dashHandler->resetReactiveCounters();
        prefs.remove("rn_ns");
        prefs.remove("rn_rb");
        prefs.remove("rn_pw");
        prefs.remove("rn_es");
        reactiveCountersLoaded = true;
        lastReactiveCountersMs = millis();
        Serial.println("reactive_nag counters reset (RAM + NVS)");
    }
```

**注意 setReactiveCounters 签名变了（v2 4 参：ns,rb,pw,es）**：base virtual + Legacy override 同步改（handlers.h 里 `virtual void setReactiveCounters(uint32_t,uint32_t,uint32_t,uint32_t)` + Legacy `nag.setCounters(ns,rb,pw,es)`）。

- [ ] **Step 4: 改 base + Legacy setReactiveCounters 签名**

`include/handlers.h`：
```cpp
    // base (CarManagerBase)
    virtual void setReactiveCounters(uint32_t /*ns*/, uint32_t /*rb*/, uint32_t /*pw*/, uint32_t /*es*/) {}
    // Legacy override
    void setReactiveCounters(uint32_t ns, uint32_t rb, uint32_t pw, uint32_t es) override
    {
        nag.setCounters(ns, rb, pw, es);
    }
```
（删 v1 的 5 参 setReactiveCounters。）

- [ ] **Step 5: 构建 + 提交**

```bash
pio test -e native_legacy -e native_reactive_nag 2>&1 | grep -E "test cases|FAILED"
pio run -e waveshare_single_can_standalone 2>&1 | grep -E "SUCCESS|FAILED|error:"
```
Expected: native 全绿；ESP SUCCESS。

```bash
git add include/handlers.h include/web/mcp2515_dashboard.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "feat(diag-v2): mode/proactiveWiggles/reactiveBursts + NVS flush 2s

reactive_nag + /status 暴露 v2 字段（mode/currentAmp/nextProactiveInMs/
reactiveBursts/proactiveWiggles）；NVS flush 5s→2s（减断电损失）；keys 改 4 参
(rn_ns/rn_rb/rn_pw/rn_es)；setReactiveCounters 签名 5→4 参。"
```

---

## Task 4: 契约测试 v2

**Files:**
- Modify: `test/test_no_epas_nag_contract.py`

- [ ] **Step 1: 更新反应式契约为 v2**

`test/test_no_epas_nag_contract.py` 的 `test_reactive_nag_is_optin_and_bounded` 适配 v2（删 v1 的 kMaxBursts/kCooldownMs 引用，改 v2 常量 + shouldEcho + active）：

```python
def test_reactive_nag_is_optin_and_bounded():
    """DashReactiveNagBurst v2 must be opt-in + bounded: bionicSteering defaults false,
    echo gated on active+shouldEcho, amplitude capped, REACTIVE gap bounded, NAG-clear
    resets. Replaces the lifted 0x370 ban (Jordan 2026-06-25)."""
    h = (ROOT / "include" / "handlers.h").read_text()
    rn = (ROOT / "include" / "dash_reactive_nag.h").read_text()

    assert re.search(r"Shared<bool>\s+bionicSteering\s*\{[^}]*\bfalse\b", h), \
        "bionicSteering must default to false"
    assert "bool useReactive = active && nag.shouldEcho(nowMs)" in h, \
        "reactive echo must be gated on active + shouldEcho"
    assert "(frame.data[5] >> 2) & 0x0F" in h, "0x399 handsOn decode must read byte5 bits[5:2]"
    assert "(frame.data[4] & 0x3F) | 0x40" in h, "data[4] handsOnLevel=1 must be forged (C)"

    assert "DashReactiveNagBurst" in rn
    assert "kAmplitudeCap{95}" in rn, "amplitude hard cap must exist"
    assert "kReactiveGapMs{800}" in rn, "REACTIVE intra-episode gap must be bounded"
    assert "kHumanWeight{8}" in rn
    # v1 stale-cooldown symbols removed
    assert "kMaxBursts" not in rn, "v1 3-burst-cooldown must be gone (rec8 fix)"
    assert "kCooldownMs" not in rn, "v1 3s cooldown must be gone (rec8 fix)"
```

- [ ] **Step 2: 跑 + 提交**

```bash
python3 -m pytest test/test_no_epas_nag_contract.py -q
```
Expected: PASS。

```bash
git add test/test_no_epas_nag_contract.py
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "test(contract-v2): 反应式契约适配 v2（shouldEcho/data[4] forge/删 v1 冷却常量）"
```

---

## Task 5: 全量验证 + 资产构建

- [ ] **Step 1: 全 native + pytest + ESP 构建**

```bash
pio test -e native_legacy -e native_reactive_nag -e native_nag -e native_dashboard 2>&1 | grep -E "test cases|FAILED"
python3 -m pytest test/ -q 2>&1 | tail -3
pio run -e waveshare_single_can_standalone 2>&1 | grep -E "SUCCESS|FAILED|RAM:"
```
Expected: native 全绿；pytest 全绿；ESP SUCCESS。

- [ ] **Step 2: clang-format + 版本戳**

```bash
clang-format -i include/dash_reactive_nag.h include/handlers.h include/web/mcp2515_dashboard.h
pio run -t clean && pio run -e waveshare_single_can_standalone 2>&1 | grep -E "SUCCESS|RAM:"
```
验证 firmware.bin version 戳含新 commit（清 .pio 缓存重建）。

- [ ] **Step 3: 构建 v2 烧录资产**

```bash
bash scripts/build_release_assets.sh "" "release-assets-reactive-nag-v2" "1.0.4-reactive-v2"
```
验证 8 资产 + SHA256SUMS。

- [ ] **Step 4: 最终 commit（若有格式修正）**

```bash
git add -A
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "chore: clang-format + 全量验证绿（反应式 NAG v2 完成）" || echo "无改动"
```

---

## 自检（实施完成后 controller 跑）

对照 spec 每节：
- §2.1 三模式 → Task 1 ✓
- §2.2 REACTIVE 持续几何（积分>0）→ Task 1（test_reactive_hold_positive_integral）✓
- §2.3 删冷却 + 800ms + 清除全复位 → Task 1（test_nag_clear_resets / test_reactive_intra_episode_gap）✓
- §2.4 PROACTIVE 周期 → Task 1（test_proactive_periodic_hold）✓
- §2.5 data[4] forge → Task 2（集成测 + 契约）✓
- §2.6 byte-exact → Task 2（counter/checksum 测）✓
- §3 架构 → Task 1-2 ✓
- §4 安全 → 默认 OFF + 门控（Task 2）+ 实车待 Jordan ✓
- §5 诊断 + NVS → Task 3 ✓
- §6 测试 → Task 1（单元）+ Task 2（集成）+ Task 4（契约）+ Task 5（全量）✓
- §7 验证 → Task 5 桌面+构建；台架/实车待 Jordan ✓

**台架 + 实车（Task 5 之后，Jordan 执行）**：烧板 → 回放 can_recording(8/9/10).csv 验三模式/proactive 周期/持续几何/计数持久化 → 安全区实车（手扶盘，故障即关）→ 录 reactive_nag + CAN 对比 v1 清除率。
