# Human Torque Replay v3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace v2 short DC burst NAG suppression with a bounded Human Torque Replay v3 profile that mimics Jordan's real manual NAG-clear 0x370 torque signatures.

**Architecture:** Keep the feature inside the existing Legacy-only Reactive NAG path. Replace `DashReactiveNagBurst` internals with a frame-driven replay state machine while preserving the public `reactive_nag` command and `/status.reactiveNag` diagnostics. LegacyHandler continues to read 0x399 HOS and echo 0x370 with counter/checksum, but now applies deterministic ± raw torque replay profiles with attempts, observation, and cooldown.

**Tech Stack:** C++17, PlatformIO native Unity tests, ESP-IDF/Arduino compatibility layer, Python unittest contract tests, existing Waveshare single-CAN firmware build.

---

## File Structure

**Modify:**
- `include/dash_reactive_nag.h` — Replace v2 IDLE/PROACTIVE/REACTIVE burst engine with Human Torque Replay v3 state machine, profiles, signed 12-bit torque helpers, diagnostics, and persistence counters.
- `include/handlers.h` — Keep `LegacyHandler::nag`, update calls from `computeHold/applyToFrame` to replay delta/signed torque helpers, expand `DashReactiveDiag` pass-through, and preserve checkAD/APActive/toggle gates.
- `include/web/mcp2515_dashboard.h` — Extend `/status.reactiveNag` JSON and serial `reactive_nag` output with v3 fields; keep existing NVS counter keys compatible where reasonable.
- `test/test_native_reactive_nag/test_main.cpp` — Replace v2 tests with v3 RED/GREEN tests for replay state, profile stepping, attempt escalation, success, cooldown, clamping, and diagnostics.
- `test/test_native_legacy/test_legacy_handler.cpp` — Replace v2 integration expectations with v3 0x370 replay expectations: positive/negative profile delta, HOS clear stops echo, retry direction, max attempts/cooldown.
- `test/test_dashboard_api_contract.py` — Add/adjust diagnostics contract for v3 `/status.reactiveNag` field names.
- `test/test_no_epas_nag_contract.py` — Ensure safety contract covers bounded v3 profile and still forbids 0x399 spoofing.

**Do not modify unless a task explicitly says so:**
- UI visual layout: existing NAG toggle can keep current label.
- Non-Legacy handlers: HW3/HW4 are out of scope.
- Non-0x370 routes such as 0x3C2 volume/speed spoof.

---

## Task 1: Pure v3 replay state machine tests (RED)

**Files:**
- Modify: `test/test_native_reactive_nag/test_main.cpp`
- Read-only context: `include/dash_reactive_nag.h`

- [ ] **Step 1: Replace v2 unit tests with v3 RED tests**

Replace the entire body of `test/test_native_reactive_nag/test_main.cpp` with this test file. It intentionally references v3 names that do not exist yet (`HumanReplayMode`, `DashHumanReplayProfileId`, `nextReplayDelta`, etc.) so it should fail before implementation.

```cpp
#include <unity.h>
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

static void feedFrames(DashReactiveNagBurst &n, int count, unsigned long startMs = 1000)
{
    for (int i = 0; i < count; ++i)
    {
        TEST_ASSERT_TRUE(n.shouldEcho(startMs + i * 40));
        (void)n.nextReplayDelta(startMs + i * 40);
    }
}

void test_inactive_hos_does_not_start_replay()
{
    DashReactiveNagBurst n;
    n.init(12345);
    n.onNagSample(3, 100, false);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL_UINT32(1, n.nagSamples());
    TEST_ASSERT_EQUAL_UINT32(0, n.replayAttempts());
    TEST_ASSERT_EQUAL_STRING("toggle", n.blockedReason());
}

void test_hos3_active_starts_first_attempt_with_medium_profile()
{
    DashReactiveNagBurst n;
    n.init(1);
    n.noteBaseTorqueRaw(12);
    n.onNagSample(3, 100, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL_UINT32(1, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::POS_MED, n.lastProfileId());
    TEST_ASSERT_EQUAL_INT(1, n.lastProfileDir());
    TEST_ASSERT_EQUAL_INT(145, n.lastPeakRaw());
}

void test_profile_outputs_exact_positive_medium_sequence()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(10);
    n.onNagSample(3, 100, true);

    const int expected[] = {40, 80, 110, 130, 145, 145, 130, 105, 75, 40};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        TEST_ASSERT_TRUE(n.shouldEcho(100 + i * 40));
        TEST_ASSERT_EQUAL_INT(expected[i], n.nextReplayDelta(100 + i * 40));
    }
    TEST_ASSERT_FALSE(n.shouldEcho(100 + 10 * 40));
    TEST_ASSERT_EQUAL(HumanReplayMode::OBSERVING, n.mode());
}

void test_failed_attempt_retries_opposite_direction()
{
    DashReactiveNagBurst n;
    n.init(3);
    n.noteBaseTorqueRaw(15);
    n.onNagSample(3, 100, true);
    feedFrames(n, 10, 100);

    n.onNagSample(3, 600, true); // first observation sample, still NAG
    TEST_ASSERT_EQUAL(HumanReplayMode::OBSERVING, n.mode());
    n.onNagSample(3, 1100, true); // second observation sample -> attempt 2

    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_EQUAL_UINT32(2, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::NEG_MED, n.lastProfileId());
    TEST_ASSERT_EQUAL_INT(-1, n.lastProfileDir());
    TEST_ASSERT_TRUE(n.nextReplayDelta(1100) < 0);
}

void test_third_attempt_uses_strong_profile_then_cooldown()
{
    DashReactiveNagBurst n;
    n.init(4);
    n.noteBaseTorqueRaw(0);
    n.onNagSample(3, 100, true);
    feedFrames(n, 10, 100);
    n.onNagSample(3, 600, true);
    n.onNagSample(3, 1100, true);
    feedFrames(n, 10, 1100);
    n.onNagSample(3, 1600, true);
    n.onNagSample(3, 2100, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_EQUAL_UINT32(3, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::POS_STRONG, n.lastProfileId());
    TEST_ASSERT_EQUAL_INT(175, n.lastPeakRaw());

    feedFrames(n, 10, 2100);
    n.onNagSample(3, 2600, true);
    n.onNagSample(3, 3100, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(3100));
    TEST_ASSERT_TRUE(n.cooldownRemainMs(3100) > 0);
    TEST_ASSERT_EQUAL_STRING("maxAttempts", n.blockedReason());
}

void test_hos_clear_records_success_and_stops_replay()
{
    DashReactiveNagBurst n;
    n.init(5);
    n.noteBaseTorqueRaw(0);
    n.onNagSample(3, 100, true);
    TEST_ASSERT_TRUE(n.shouldEcho(100));

    n.onNagSample(2, 180, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(180));
    TEST_ASSERT_EQUAL_UINT32(1, n.replaySuccesses());
    TEST_ASSERT_EQUAL_UINT8(3, n.lastHosBefore());
    TEST_ASSERT_EQUAL_UINT8(2, n.lastHosAfter());
}

void test_apply_delta_to_legacy_torque_supports_negative_and_clamps()
{
    DashReactiveNagBurst n;
    n.init(6);

    uint8_t d2lo = 0x08;
    uint8_t d3 = 0x00; // signed 0
    n.applyDeltaToFrame(d2lo, d3, -175);
    int signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(-175, signedOut);

    d2lo = 0x08;
    d3 = 0x00;
    n.applyDeltaToFrame(d2lo, d3, 999);
    signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(220, signedOut);

    d2lo = 0x08;
    d3 = 0x00;
    n.applyDeltaToFrame(d2lo, d3, -999);
    signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(-220, signedOut);
}

void test_diag_reports_v3_fields()
{
    DashReactiveNagBurst n;
    n.init(7);
    n.noteBaseTorqueRaw(12);
    n.onNagSample(3, 100, true);
    (void)n.nextReplayDelta(100);
    n.notifyEchoSent();

    DashReactiveDiag d = n.diag(140);
    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, d.mode);
    TEST_ASSERT_TRUE(d.injecting);
    TEST_ASSERT_EQUAL_UINT8(3, d.lastHandsOnState);
    TEST_ASSERT_EQUAL_UINT32(1, d.nagSamples);
    TEST_ASSERT_EQUAL_UINT32(1, d.replayAttempts);
    TEST_ASSERT_EQUAL_UINT32(0, d.replaySuccesses);
    TEST_ASSERT_EQUAL_INT(40, d.lastOutDeltaRaw);
    TEST_ASSERT_EQUAL_INT(145, d.lastPeakRaw);
    TEST_ASSERT_EQUAL_INT(12, d.lastBaseRaw);
    TEST_ASSERT_EQUAL_UINT32(1, d.echoSent);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_inactive_hos_does_not_start_replay);
    RUN_TEST(test_hos3_active_starts_first_attempt_with_medium_profile);
    RUN_TEST(test_profile_outputs_exact_positive_medium_sequence);
    RUN_TEST(test_failed_attempt_retries_opposite_direction);
    RUN_TEST(test_third_attempt_uses_strong_profile_then_cooldown);
    RUN_TEST(test_hos_clear_records_success_and_stops_replay);
    RUN_TEST(test_apply_delta_to_legacy_torque_supports_negative_and_clamps);
    RUN_TEST(test_diag_reports_v3_fields);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the native reactive test and confirm RED**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
pio test -e native_reactive_nag
```

Expected: compilation fails with missing `HumanReplayMode`, `DashHumanReplayProfileId`, `nextReplayDelta`, `noteBaseTorqueRaw`, and v3 diagnostic fields. This proves the tests are exercising new behavior.

- [ ] **Step 3: Commit the RED tests**

```bash
git add test/test_native_reactive_nag/test_main.cpp
git commit -m "test: define human torque replay state machine behavior"
```

---

## Task 2: Implement v3 state machine in `dash_reactive_nag.h`

**Files:**
- Modify: `include/dash_reactive_nag.h`
- Test: `test/test_native_reactive_nag/test_main.cpp`

- [ ] **Step 1: Replace the v2 types and state machine with v3 definitions**

In `include/dash_reactive_nag.h`, replace the current `NagMode`, `DashReactiveDiag`, and `DashReactiveNagBurst` definitions with this implementation. Keep the file name `dash_reactive_nag.h` to minimize include churn.

```cpp
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef NATIVE_BUILD
#include <Arduino.h>
#else
#include <algorithm>
#endif

struct DashReactivePRNG
{
    uint32_t s{0xA5A5A5A5u};
    void seed(uint32_t v) { s = v ? v : 0xA5A5A5A5u; }
    uint32_t next()
    {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = x ? x : 0xA5A5A5A5u;
        return s;
    }
    int range(int lo, int hi)
    {
        if (hi <= lo)
            return lo;
        return lo + (int)(next() % (uint32_t)(hi - lo + 1));
    }
};

enum class HumanReplayMode : uint8_t
{
    IDLE = 0,
    REPLAYING = 1,
    OBSERVING = 2,
    COOLDOWN = 3,
};

enum class DashHumanReplayProfileId : uint8_t
{
    NONE = 0,
    POS_MED = 1,
    NEG_MED = 2,
    POS_STRONG = 3,
    NEG_STRONG = 4,
};

struct DashReactiveDiag
{
    bool enabled{false};
    HumanReplayMode mode{HumanReplayMode::IDLE};
    bool injecting{false};
    uint8_t lastHandsOnState{0};
    int currentAmp{0};
    uint32_t nagSamples{0};
    uint32_t reactiveBursts{0};
    uint32_t proactiveWiggles{0};
    uint32_t echoSent{0};
    unsigned long nextProactiveInMs{0};

    uint32_t replayAttempts{0};
    uint32_t replaySuccesses{0};
    uint32_t replayFailures{0};
    DashHumanReplayProfileId lastProfileId{DashHumanReplayProfileId::NONE};
    int lastProfileDir{0};
    int lastPeakRaw{0};
    int lastBaseRaw{0};
    int lastOutDeltaRaw{0};
    uint8_t profileIndex{0};
    uint8_t lastHosBefore{0};
    uint8_t lastHosAfter{0};
    unsigned long cooldownRemainMs{0};
    const char *blockedReason{"none"};
};

struct DashReactiveNagBurst
{
    static constexpr int kNagThreshold{3};
    static constexpr int kMaxAttempts{3};
    static constexpr unsigned long kCooldownMs{3000};
    static constexpr int kMaxDeltaRaw{180};
    static constexpr int kMaxSignedOutRaw{220};
    static constexpr uint8_t kObserveSamples{2};

    static constexpr int8_t kPosMed[10] = {40, 80, 110, 130, 145, 145, 130, 105, 75, 40};
    static constexpr int8_t kNegMed[10] = {-40, -80, -110, -128, -128, -128, -128, -105, -75, -40};
    static constexpr int16_t kPosStrong[10] = {50, 100, 140, 165, 175, 170, 150, 120, 80, 45};
    static constexpr int16_t kNegStrong[10] = {-50, -100, -140, -165, -175, -170, -150, -120, -80, -45};

    DashReactivePRNG rng;
    HumanReplayMode mode_{HumanReplayMode::IDLE};
    DashHumanReplayProfileId profileId_{DashHumanReplayProfileId::NONE};
    uint8_t profileIndex_{0};
    uint8_t observeSamples_{0};
    uint8_t lastHandsOnState{0};
    uint8_t lastHosBefore_{0};
    uint8_t lastHosAfter_{0};
    bool nagActive_{false};
    unsigned long cooldownUntilMs_{0};
    const char *blockedReason_{"none"};
    int lastBaseRaw_{0};
    int lastOutDeltaRaw_{0};
    int lastPeakRaw_{0};
    int lastProfileDir_{0};
    bool preferPositive_{true};

    uint32_t nagSamples_{0};
    uint32_t replayAttempts_{0};
    uint32_t replaySuccesses_{0};
    uint32_t replayFailures_{0};
    uint32_t echoSent_{0};

    void init(uint32_t seed)
    {
        rng.seed(seed);
        preferPositive_ = (rng.next() & 1u) == 0;
    }

    void reset()
    {
        mode_ = HumanReplayMode::IDLE;
        profileId_ = DashHumanReplayProfileId::NONE;
        profileIndex_ = 0;
        observeSamples_ = 0;
        lastHandsOnState = 0;
        lastHosBefore_ = 0;
        lastHosAfter_ = 0;
        nagActive_ = false;
        cooldownUntilMs_ = 0;
        blockedReason_ = "none";
        lastOutDeltaRaw_ = 0;
        lastPeakRaw_ = 0;
        lastProfileDir_ = 0;
    }

    void resetCounters()
    {
        nagSamples_ = 0;
        replayAttempts_ = 0;
        replaySuccesses_ = 0;
        replayFailures_ = 0;
        echoSent_ = 0;
    }

    void bumpCounters()
    {
        nagSamples_ += 111;
        replayAttempts_ += 222;
        replaySuccesses_ += 333;
        echoSent_ += 444;
    }

    void setCounters(uint32_t ns, uint32_t attempts, uint32_t successes, uint32_t es)
    {
        nagSamples_ = ns;
        replayAttempts_ = attempts;
        replaySuccesses_ = successes;
        echoSent_ = es;
    }

    void notifyEchoSent() { echoSent_++; }
    void noteBaseTorqueRaw(int raw) { lastBaseRaw_ = raw; }

    HumanReplayMode mode() const { return mode_; }
    bool shouldEcho(unsigned long nowMs) const { return mode_ == HumanReplayMode::REPLAYING && nowMs >= 0; }
    uint32_t nagSamples() const { return nagSamples_; }
    uint32_t replayAttempts() const { return replayAttempts_; }
    uint32_t replaySuccesses() const { return replaySuccesses_; }
    uint32_t replayFailures() const { return replayFailures_; }
    DashHumanReplayProfileId lastProfileId() const { return profileId_; }
    int lastProfileDir() const { return lastProfileDir_; }
    int lastPeakRaw() const { return lastPeakRaw_; }
    int lastOutDeltaRaw() const { return lastOutDeltaRaw_; }
    const char *blockedReason() const { return blockedReason_; }
    uint8_t lastHosBefore() const { return lastHosBefore_; }
    uint8_t lastHosAfter() const { return lastHosAfter_; }
    unsigned long cooldownRemainMs(unsigned long nowMs) const
    {
        return (mode_ == HumanReplayMode::COOLDOWN && cooldownUntilMs_ > nowMs) ? (cooldownUntilMs_ - nowMs) : 0;
    }

    static int clampInt(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static int decodeSignedTorque(uint8_t data2Lo, uint8_t data3)
    {
        int raw = ((int)(data2Lo & 0x0F) << 8) | data3;
        return raw - 0x800;
    }

    static uint16_t encodeSignedTorque(int signedRaw)
    {
        int clamped = clampInt(signedRaw, -2048, 2047);
        return (uint16_t)((clamped + 0x800) & 0x0FFF);
    }

    void applyDeltaToFrame(uint8_t &data2Lo, uint8_t &data3, int deltaRaw)
    {
        int signedBase = decodeSignedTorque(data2Lo, data3);
        int out = clampInt(signedBase + clampInt(deltaRaw, -kMaxDeltaRaw, kMaxDeltaRaw),
                           -kMaxSignedOutRaw, kMaxSignedOutRaw);
        uint16_t enc = encodeSignedTorque(out);
        data2Lo = (uint8_t)((enc >> 8) & 0x0F);
        data3 = (uint8_t)(enc & 0xFF);
        lastOutDeltaRaw_ = out - signedBase;
    }

    int profileLength() const { return 10; }

    int profileDeltaAt(uint8_t idx) const
    {
        if (idx >= 10)
            return 0;
        switch (profileId_)
        {
        case DashHumanReplayProfileId::POS_MED:
            return kPosMed[idx];
        case DashHumanReplayProfileId::NEG_MED:
            return kNegMed[idx];
        case DashHumanReplayProfileId::POS_STRONG:
            return kPosStrong[idx];
        case DashHumanReplayProfileId::NEG_STRONG:
            return kNegStrong[idx];
        default:
            return 0;
        }
    }

    void startAttempt(unsigned long nowMs)
    {
        (void)nowMs;
        replayAttempts_++;
        observeSamples_ = 0;
        profileIndex_ = 0;
        mode_ = HumanReplayMode::REPLAYING;
        blockedReason_ = "none";
        lastHosBefore_ = lastHandsOnState;

        if (replayAttempts_ == 1)
            profileId_ = preferPositive_ ? DashHumanReplayProfileId::POS_MED : DashHumanReplayProfileId::NEG_MED;
        else if (replayAttempts_ == 2)
            profileId_ = preferPositive_ ? DashHumanReplayProfileId::NEG_MED : DashHumanReplayProfileId::POS_MED;
        else
            profileId_ = preferPositive_ ? DashHumanReplayProfileId::POS_STRONG : DashHumanReplayProfileId::NEG_STRONG;

        lastProfileDir_ = (profileId_ == DashHumanReplayProfileId::POS_MED ||
                           profileId_ == DashHumanReplayProfileId::POS_STRONG)
                              ? 1
                              : -1;
        lastPeakRaw_ = 0;
        for (int i = 0; i < profileLength(); ++i)
        {
            int v = profileDeltaAt((uint8_t)i);
            if (v < 0)
                v = -v;
            if (v > lastPeakRaw_)
                lastPeakRaw_ = v;
        }
    }

    int nextReplayDelta(unsigned long nowMs)
    {
        (void)nowMs;
        if (mode_ != HumanReplayMode::REPLAYING)
            return 0;
        int delta = profileDeltaAt(profileIndex_);
        lastOutDeltaRaw_ = delta;
        profileIndex_++;
        if (profileIndex_ >= profileLength())
        {
            mode_ = HumanReplayMode::OBSERVING;
            profileIndex_ = 0;
            observeSamples_ = 0;
        }
        return delta;
    }

    void onNagSample(uint8_t hos, unsigned long nowMs, bool active)
    {
        lastHandsOnState = hos;
        if (hos >= kNagThreshold)
            nagSamples_++;

        if (hos <= 2)
        {
            if (nagActive_ && (mode_ == HumanReplayMode::REPLAYING || mode_ == HumanReplayMode::OBSERVING))
            {
                replaySuccesses_++;
                lastHosAfter_ = hos;
                preferPositive_ = lastProfileDir_ >= 0;
            }
            nagActive_ = false;
            mode_ = HumanReplayMode::IDLE;
            profileId_ = DashHumanReplayProfileId::NONE;
            profileIndex_ = 0;
            observeSamples_ = 0;
            blockedReason_ = "none";
            replayAttempts_ = 0;
            return;
        }

        if (hos < kNagThreshold)
            return;

        nagActive_ = true;
        lastHosAfter_ = hos;
        if (!active)
        {
            blockedReason_ = "toggle";
            return;
        }
        if (mode_ == HumanReplayMode::COOLDOWN)
        {
            if (cooldownUntilMs_ > nowMs)
            {
                blockedReason_ = "maxAttempts";
                return;
            }
            mode_ = HumanReplayMode::IDLE;
            replayAttempts_ = 0;
            blockedReason_ = "none";
        }
        if (mode_ == HumanReplayMode::IDLE)
        {
            lastHosBefore_ = hos;
            startAttempt(nowMs);
            return;
        }
        if (mode_ == HumanReplayMode::OBSERVING)
        {
            observeSamples_++;
            if (observeSamples_ >= kObserveSamples)
            {
                replayFailures_++;
                if (replayAttempts_ >= kMaxAttempts)
                {
                    mode_ = HumanReplayMode::COOLDOWN;
                    cooldownUntilMs_ = nowMs + kCooldownMs;
                    blockedReason_ = "maxAttempts";
                    return;
                }
                startAttempt(nowMs);
            }
        }
    }

    DashReactiveDiag diag(unsigned long nowMs) const
    {
        DashReactiveDiag d;
        d.mode = mode_;
        d.injecting = mode_ == HumanReplayMode::REPLAYING;
        d.lastHandsOnState = lastHandsOnState;
        d.currentAmp = lastPeakRaw_;
        d.nagSamples = nagSamples_;
        d.reactiveBursts = replayAttempts_;
        d.proactiveWiggles = replaySuccesses_;
        d.echoSent = echoSent_;
        d.nextProactiveInMs = cooldownRemainMs(nowMs);
        d.replayAttempts = replayAttempts_;
        d.replaySuccesses = replaySuccesses_;
        d.replayFailures = replayFailures_;
        d.lastProfileId = profileId_;
        d.lastProfileDir = lastProfileDir_;
        d.lastPeakRaw = lastPeakRaw_;
        d.lastBaseRaw = lastBaseRaw_;
        d.lastOutDeltaRaw = lastOutDeltaRaw_;
        d.profileIndex = profileIndex_;
        d.lastHosBefore = lastHosBefore_;
        d.lastHosAfter = lastHosAfter_;
        d.cooldownRemainMs = cooldownRemainMs(nowMs);
        d.blockedReason = blockedReason_;
        return d;
    }
};
```

- [ ] **Step 2: Run the reactive native test and confirm GREEN**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
pio test -e native_reactive_nag
```

Expected: all 8 tests pass.

- [ ] **Step 3: Commit state machine implementation**

```bash
git add include/dash_reactive_nag.h test/test_native_reactive_nag/test_main.cpp
git commit -m "feat: add human torque replay nag state machine"
```

---

## Task 3: LegacyHandler v3 integration tests (RED)

**Files:**
- Modify: `test/test_native_legacy/test_legacy_handler.cpp`
- Read-only context: `include/handlers.h`

- [ ] **Step 1: Replace v2 Legacy reactive tests with v3 tests**

In `test/test_native_legacy/test_legacy_handler.cpp`, replace the current Reactive NAG section (`test_legacy_reactive_off_no_echo`, `test_legacy_reactive_v2_nag_echo_sustained_and_forge`, `test_legacy_reactive_no_nag_proactive_echo`, `test_legacy_reactive_checkad_blocks`) with this v3 section. Also update the `RUN_TEST(...)` calls at the bottom to match these names.

```cpp
// ============================================================
// Human Torque Replay NAG suppression v3 — opt-in via bionicSteering, LegacyHandler only.
// ============================================================

void test_legacy_replay_off_no_echo()
{
    handler.bionicSteering = false;
    CanFrame das = makeDasFrame(3);
    CanFrame epas = makeEpasFrame(0, 0.10, 0x0C);
    handler.handleMessage(das, mock);
    handler.handleMessage(epas, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_legacy_replay_hos3_sends_positive_profile_frame()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrame(3);
    CanFrame epas = makeEpasFrame(0, 0.10, 0x0C); // signed base around +10 raw

    handler.handleMessage(das, mock);
    handler.handleMessage(epas, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    CanFrame e = mock.sent[0];
    int32_t out = decodeEchoTorqueRaw(e) - 0x800;
    TEST_ASSERT_TRUE(out >= 40);
    TEST_ASSERT_TRUE(out <= 220);
    TEST_ASSERT_TRUE((e.data[4] & 0xC0) == 0x40);
    TEST_ASSERT_EQUAL_UINT8(((0x0C + 1) & 0x0F), (e.data[6] & 0x0F));
    uint16_t sum = 0;
    for (int i = 0; i < 7; ++i)
        sum += e.data[i];
    TEST_ASSERT_EQUAL_UINT8((sum + 0x73) & 0xFF, e.data[7]);
}

void test_legacy_replay_profile_stops_after_ten_frames_until_observation()
{
    handler.bionicSteering = true;
    handler.handleMessage(makeDasFrame(3), mock);
    for (int i = 0; i < 12; ++i)
        handler.handleMessage(makeEpasFrame(0, 0.10, (uint8_t)(i & 0x0F)), mock);

    TEST_ASSERT_EQUAL(10, mock.sent.size());
    DashReactiveDiag d = handler.reactiveDiag();
    TEST_ASSERT_EQUAL(HumanReplayMode::OBSERVING, d.mode);
}

void test_legacy_replay_hos_clear_stops_future_echo()
{
    handler.bionicSteering = true;
    handler.handleMessage(makeDasFrame(3), mock);
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x01), mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    handler.handleMessage(makeDasFrame(2), mock);
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x02), mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.reactiveDiag().replaySuccesses);
}

void test_legacy_replay_retry_can_emit_negative_profile()
{
    handler.bionicSteering = true;
    handler.handleMessage(makeDasFrame(3), mock);
    for (int i = 0; i < 10; ++i)
        handler.handleMessage(makeEpasFrame(0, 0.10, (uint8_t)(i & 0x0F)), mock);
    handler.handleMessage(makeDasFrame(3), mock);
    handler.handleMessage(makeDasFrame(3), mock);

    size_t before = mock.sent.size();
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x0B), mock);
    TEST_ASSERT_EQUAL(before + 1, mock.sent.size());
    CanFrame e = mock.sent.back();
    int32_t out = decodeEchoTorqueRaw(e) - 0x800;
    TEST_ASSERT_TRUE(out < 0);
    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, handler.reactiveDiag().mode);
}

void test_legacy_replay_checkad_blocks()
{
    handler.bionicSteering = true;
    handler.checkAD = denyAD;
    handler.handleMessage(makeDasFrame(3), mock);
    handler.handleMessage(makeEpasFrame(0, 0.10, 0x0C), mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    handler.checkAD = nullptr;
}
```

At the bottom of the file, replace the old `RUN_TEST(test_legacy_reactive_...)` calls with:

```cpp
    RUN_TEST(test_legacy_replay_off_no_echo);
    RUN_TEST(test_legacy_replay_hos3_sends_positive_profile_frame);
    RUN_TEST(test_legacy_replay_profile_stops_after_ten_frames_until_observation);
    RUN_TEST(test_legacy_replay_hos_clear_stops_future_echo);
    RUN_TEST(test_legacy_replay_retry_can_emit_negative_profile);
    RUN_TEST(test_legacy_replay_checkad_blocks);
```

- [ ] **Step 2: Run legacy native test and confirm RED**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
pio test -e native_legacy
```

Expected: failures in the new v3 Legacy tests because `LegacyHandler` still calls v2 `computeHold/applyToFrame` APIs.

- [ ] **Step 3: Commit RED integration tests**

```bash
git add test/test_native_legacy/test_legacy_handler.cpp
git commit -m "test: define legacy human replay integration"
```

---

## Task 4: Integrate v3 replay into `LegacyHandler`

**Files:**
- Modify: `include/handlers.h`
- Test: `test/test_native_legacy/test_legacy_handler.cpp`

- [ ] **Step 1: Update `LegacyHandler` reactiveDiag comments and counters only if needed**

Keep the existing `LegacyHandler::nag` member and existing `reactiveDiag()`, `resetReactiveCounters()`, `setReactiveCounters()`, and `bumpReactiveCounters()` methods. The v3 implementation keeps these method names for compatibility with NVS and serial commands.

No code change is required in these methods unless compilation fails.

- [ ] **Step 2: Replace the 0x370 v2 echo body**

In `include/handlers.h`, inside `LegacyHandler::handleMessage`, replace the current `if (frame.id == 880 && frame.dlc >= 8)` block body with this v3 version:

```cpp
        if (frame.id == 880 && frame.dlc >= 8)
        {
            unsigned long nowMs = dashDiagNowMs();
            bool active = (bool)bionicSteering && APActive;
            bool useReplay = active && nag.shouldEcho(nowMs);
            if (checkAD && !checkAD())
                useReplay = false;
            if (useReplay)
            {
                CanFrame echo;
                echo.id = 880;
                echo.dlc = 8;
                echo.data[0] = frame.data[0];
                echo.data[1] = frame.data[1];

                uint8_t d2lo = frame.data[2] & 0x0F;
                uint8_t d3 = frame.data[3];
                int signedBase = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
                nag.noteBaseTorqueRaw(signedBase);
                int delta = nag.nextReplayDelta(nowMs);
                nag.applyDeltaToFrame(d2lo, d3, delta);
                echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | d2lo);
                echo.data[3] = d3;

                echo.data[4] = static_cast<uint8_t>((frame.data[4] & 0x3F) | 0x40);
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

- [ ] **Step 3: Keep 0x399 sampling but ensure active gates are passed**

Verify the existing 0x399 block remains:

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
                bool active = (bool)bionicSteering && APActive;
                nag.onNagSample(hos, dashDiagNowMs(), active);
            }
            return;
        }
```

Do not add 0x399 transmit/spoof logic.

- [ ] **Step 4: Run native tests for reactive and legacy**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
pio test -e native_reactive_nag
pio test -e native_legacy
```

Expected: both test suites pass.

- [ ] **Step 5: Commit Legacy integration**

```bash
git add include/handlers.h include/dash_reactive_nag.h test/test_native_legacy/test_legacy_handler.cpp
git commit -m "feat: wire human torque replay into legacy handler"
```

---

## Task 5: Extend serial and `/status.reactiveNag` diagnostics

**Files:**
- Modify: `include/web/mcp2515_dashboard.h`
- Test: `test/test_dashboard_api_contract.py`

- [ ] **Step 1: Add dashboard contract tests for v3 diagnostics**

Append these tests to `test/test_dashboard_api_contract.py` near existing reactive NAG contract tests:

```python
    def test_reactive_nag_v3_status_exposes_replay_diagnostics(self) -> None:
        """Human Torque Replay v3 must expose attempt/profile/HOS diagnostics."""
        required = [
            '"replayAttempts"',
            '"replaySuccesses"',
            '"replayFailures"',
            '"lastProfileId"',
            '"lastProfileDir"',
            '"lastPeakRaw"',
            '"lastBaseRaw"',
            '"lastOutDeltaRaw"',
            '"profileIndex"',
            '"lastHosBefore"',
            '"lastHosAfter"',
            '"cooldownRemainMs"',
            '"blockedReason"',
        ]
        for token in required:
            self.assertIn(token, self.dash)

    def test_reactive_nag_serial_command_prints_v3_replay_fields(self) -> None:
        """Serial reactive_nag output should be enough for post-drive diagnosis."""
        required = [
            "replayAttempts=",
            "replaySuccesses=",
            "replayFailures=",
            "lastProfileId=",
            "lastProfileDir=",
            "lastPeakRaw=",
            "lastBaseRaw=",
            "lastOutDeltaRaw=",
            "blockedReason=",
        ]
        for token in required:
            self.assertIn(token, self.dash)
```

- [ ] **Step 2: Run contract tests and confirm RED**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
PYTHONPATH=test python3 -m unittest test_dashboard_api_contract.TestDashboardApiContract.test_reactive_nag_v3_status_exposes_replay_diagnostics test_dashboard_api_contract.TestDashboardApiContract.test_reactive_nag_serial_command_prints_v3_replay_fields
```

Expected: fails because new JSON and serial fields are not yet printed.

- [ ] **Step 3: Extend `/status.reactiveNag` JSON**

In `include/web/mcp2515_dashboard.h`, inside the existing `j += ",\"reactiveNag\":{";` block, after `echoSent`, append:

```cpp
        j += ",\"replayAttempts\":";
        j += String(d.replayAttempts);
        j += ",\"replaySuccesses\":";
        j += String(d.replaySuccesses);
        j += ",\"replayFailures\":";
        j += String(d.replayFailures);
        j += ",\"lastProfileId\":";
        j += String((int)d.lastProfileId);
        j += ",\"lastProfileDir\":";
        j += String(d.lastProfileDir);
        j += ",\"lastPeakRaw\":";
        j += String(d.lastPeakRaw);
        j += ",\"lastBaseRaw\":";
        j += String(d.lastBaseRaw);
        j += ",\"lastOutDeltaRaw\":";
        j += String(d.lastOutDeltaRaw);
        j += ",\"profileIndex\":";
        j += String((int)d.profileIndex);
        j += ",\"lastHosBefore\":";
        j += String((int)d.lastHosBefore);
        j += ",\"lastHosAfter\":";
        j += String((int)d.lastHosAfter);
        j += ",\"cooldownRemainMs\":";
        j += String(d.cooldownRemainMs);
        j += ",\"blockedReason\":\"";
        j += jsonEscape(d.blockedReason ? d.blockedReason : "none");
        j += "\"";
```

- [ ] **Step 4: Extend serial `reactive_nag` command output**

In `dashSerialRunCommand`, in the `reactive_nag` command branch, after the existing `nagSamples/reactiveBursts/proactiveWiggles/echoSent` print, add:

```cpp
            Serial.printf("replayAttempts=%lu replaySuccesses=%lu replayFailures=%lu lastProfileId=%d lastProfileDir=%d\n",
                          (unsigned long)d.replayAttempts, (unsigned long)d.replaySuccesses,
                          (unsigned long)d.replayFailures, (int)d.lastProfileId, d.lastProfileDir);
            Serial.printf("lastPeakRaw=%d lastBaseRaw=%d lastOutDeltaRaw=%d profileIndex=%u hosBefore=%u hosAfter=%u cooldownRemainMs=%lu blockedReason=%s\n",
                          d.lastPeakRaw, d.lastBaseRaw, d.lastOutDeltaRaw, (unsigned)d.profileIndex,
                          (unsigned)d.lastHosBefore, (unsigned)d.lastHosAfter,
                          (unsigned long)d.cooldownRemainMs, d.blockedReason ? d.blockedReason : "none");
```

- [ ] **Step 5: Preserve NVS counter compatibility**

Keep existing NVS keys:

```text
rn_ns -> nagSamples
rn_rb -> replayAttempts, still printed as reactiveBursts for backward compatibility
rn_pw -> replaySuccesses, still printed as proactiveWiggles for backward compatibility
rn_es -> echoSent
```

No new NVS keys are required for v3. This keeps `reactive_nag_bump` and previous persistence tests working.

- [ ] **Step 6: Run contract test and native dashboard build check**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
PYTHONPATH=test python3 -m unittest test_dashboard_api_contract.TestDashboardApiContract.test_reactive_nag_v3_status_exposes_replay_diagnostics test_dashboard_api_contract.TestDashboardApiContract.test_reactive_nag_serial_command_prints_v3_replay_fields
pio test -e native_single_can_dashboard
```

Expected: selected contract tests pass; native single-CAN dashboard tests pass.

- [ ] **Step 7: Commit diagnostics**

```bash
git add include/web/mcp2515_dashboard.h test/test_dashboard_api_contract.py
git commit -m "feat: expose human replay nag diagnostics"
```

---

## Task 6: Safety contract tests for v3 bounds and no 0x399 spoofing

**Files:**
- Modify: `test/test_no_epas_nag_contract.py`
- Test: `test/test_no_epas_nag_contract.py`

- [ ] **Step 1: Add v3 safety contract tests**

Append this test class to `test/test_no_epas_nag_contract.py`:

```python
class HumanTorqueReplayV3Contract(unittest.TestCase):
    def setUp(self) -> None:
        root = Path(__file__).resolve().parents[1]
        self.reactive = (root / "include" / "dash_reactive_nag.h").read_text()
        self.handlers = (root / "include" / "handlers.h").read_text()

    def test_v3_replay_has_hard_attempt_and_torque_bounds(self) -> None:
        self.assertIn("kMaxAttempts{3}", self.reactive)
        self.assertIn("kCooldownMs{3000}", self.reactive)
        self.assertIn("kMaxDeltaRaw{180}", self.reactive)
        self.assertIn("kMaxSignedOutRaw{220}", self.reactive)

    def test_v3_replay_profiles_are_bounded_and_bidirectional(self) -> None:
        for token in ["kPosMed", "kNegMed", "kPosStrong", "kNegStrong"]:
            self.assertIn(token, self.reactive)
        self.assertIn("175", self.reactive)
        self.assertIn("-175", self.reactive)
        self.assertNotIn("250", self.reactive)

    def test_v3_does_not_transmit_or_mutate_0x399(self) -> None:
        block_start = self.handlers.index("if (frame.id == 921)")
        block_end = self.handlers.index("// 0x3EE", block_start)
        block = self.handlers[block_start:block_end]
        self.assertIn("nag.onNagSample", block)
        self.assertNotIn("driver.send", block)
        self.assertNotIn("frame.data[5] =", block)
        self.assertNotIn("echo.id = 921", self.handlers)

    def test_v3_legacy_echo_remains_opt_in_and_checkad_gated(self) -> None:
        block_start = self.handlers.index("if (frame.id == 880")
        block_end = self.handlers.index("// STW_ACTN_RQ", block_start)
        block = self.handlers[block_start:block_end]
        self.assertIn("bionicSteering", block)
        self.assertIn("APActive", block)
        self.assertIn("checkAD", block)
        self.assertIn("nag.shouldEcho", block)
        self.assertIn("nag.nextReplayDelta", block)
```

If `unittest` or `Path` imports are missing at top of file, add:

```python
from pathlib import Path
import unittest
```

- [ ] **Step 2: Run safety contract tests**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
PYTHONPATH=test python3 -m unittest test_no_epas_nag_contract.HumanTorqueReplayV3Contract
```

Expected: pass.

- [ ] **Step 3: Commit safety contracts**

```bash
git add test/test_no_epas_nag_contract.py
git commit -m "test: lock human replay nag safety bounds"
```

---

## Task 7: Full validation, asset build, and handoff note

**Files:**
- Modify: `docs/HANDOFF-2026-06-25.md` only if it already tracks current NAG status; otherwise do not create a new handoff here.
- Generated local assets: `release-assets-human-replay-v3/` (untracked unless Jordan explicitly asks to commit assets)

- [ ] **Step 1: Run focused native tests**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
pio test -e native_reactive_nag
pio test -e native_legacy
pio test -e native_single_can_dashboard
```

Expected: all pass.

- [ ] **Step 2: Run Python contract suite**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
PYTHONPATH=test python3 -m unittest discover -s test -p 'test_*.py'
```

Expected: all Python tests pass.

- [ ] **Step 3: Run minify check**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
python3 scripts/minify_dashboard.py --check
```

Expected: exit 0. If it reports generated header drift, run:

```bash
python3 scripts/minify_dashboard.py
git add include/web/mcp2515_dashboard_ui.h
git commit -m "build: refresh dashboard ui header for human replay nag"
```

- [ ] **Step 4: Clean build ESP firmware**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
pio run -t clean -e waveshare_single_can_standalone
pio run -e waveshare_single_can_standalone
```

Expected: build SUCCESS.

- [ ] **Step 5: Build release assets in a separate directory**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
scripts/build_release_assets.sh .pio/build/waveshare_single_can_standalone release-assets-human-replay-v3 1.0.4-human-replay-v3
(cd release-assets-human-replay-v3 && shasum -a 256 -c SHA256SUMS)
```

Expected: 8 assets created; `SHA256SUMS` all OK.

- [ ] **Step 6: Commit final code before flashing**

Run:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git status --short
git add include/dash_reactive_nag.h include/handlers.h include/web/mcp2515_dashboard.h include/web/mcp2515_dashboard_ui.h test/test_native_reactive_nag/test_main.cpp test/test_native_legacy/test_legacy_handler.cpp test/test_dashboard_api_contract.py test/test_no_epas_nag_contract.py
git commit -m "feat: add human torque replay nag v3"
```

Expected: commit succeeds. Do not push.

- [ ] **Step 7: Flash only with explicit Jordan authorization**

If Jordan authorizes flashing, use the detected port. First list devices:

```bash
pio device list
```

Then flash, replacing the port if needed:

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware/release-assets-human-replay-v3
./flash.sh /dev/cu.usbmodem21101
```

Expected: esptool prints `Hash of data verified`.

- [ ] **Step 8: Serial smoke test after flash**

Run:

```bash
python3 - <<'PY'
import serial, time
port='/dev/cu.usbmodem21101'
ser=serial.Serial(port,115200,timeout=0.1)
ser.dtr=False; ser.rts=True; time.sleep(0.08); ser.rts=False; ser.dtr=False
buf=[]; start=time.time()
while time.time()-start<14:
    data=ser.read(4096)
    if data:
        s=data.decode('utf-8','replace')
        print(s,end='')
        buf.append(s)
        if '[DIAG] Serial commands ready' in ''.join(buf):
            break
ser.write(b'reactive_nag\n'); ser.flush()
start=time.time(); last=time.time()
while time.time()-start<3:
    data=ser.read(4096)
    if data:
        s=data.decode('utf-8','replace')
        print(s,end='')
        last=time.time()
    elif time.time()-last>0.7:
        break
ser.close()
PY
```

Expected:

```text
[BOOT] canActive=NO or persisted state explicitly shown
[CFG] Handler switched to LEGACY
[DIAG] Serial commands ready
reactive_nag output includes replayAttempts/replaySuccesses/lastProfileId/blockedReason
```

- [ ] **Step 9: Update memory after verified flash or after deciding not to flash**

Update `/Users/ziwind/.claude/projects/-Users-ziwind-my-vibe-project/memory/waveshare-single-can-bionic-port-track-a-20260625.md` with:

```text
Human Torque Replay v3 implementation status, commit SHA, validation matrix, asset directory, flash status, and next real-car test criteria.
```

Also update `/Users/ziwind/.claude/projects/-Users-ziwind-my-vibe-project/memory/MEMORY.md` first entry hook if the next handoff point changes.

---

## Self-Review Checklist

- Spec coverage: Tasks 1-2 cover state machine/profiles/clamps; Task 3-4 cover Legacy 0x399/0x370 data flow; Task 5 covers diagnostics; Task 6 covers safety/no 0x399 spoofing; Task 7 covers validation/assets/flash/memory.
- Placeholder scan: no unfinished-placeholder phrases; all code-changing steps include concrete code or exact replacement snippets.
- Type consistency: plan uses `HumanReplayMode`, `DashHumanReplayProfileId`, `DashReactiveNagBurst`, `DashReactiveDiag`, `nextReplayDelta`, `applyDeltaToFrame`, `noteBaseTorqueRaw` consistently across tests and implementation.
- Scope: no non-0x370 DND routes, no HW3/HW4 changes, no default-on behavior.
