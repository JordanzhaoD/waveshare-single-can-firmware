#include <unity.h>
#include <cstdint>
#include <initializer_list>
#include "dash_legacy_steer_defense.h"

// Wrap-safe age helper — mirrors dash_twai_diag.h dashAgeMs so this isolated
// suite stays free of the full dashboard dependency tree.
static inline uint32_t dashAgeMs(uint32_t nowMs, uint32_t eventMs)
{
    return eventMs == 0 ? 0 : nowMs - eventMs;
}

void setUp() {}
void tearDown() {}

// These tests lock the DashLegacySteerDefense semantics: the from-scratch
// Legacy steer-jerk module (beta.16 Instant edge / beta.17 Minimal 5-frame
// edge burst / beta.19 ESP32 edge / settle fallback).

void test_engaged_state_matrix()
{
    for (uint8_t s : {static_cast<uint8_t>(3), static_cast<uint8_t>(4),
                      static_cast<uint8_t>(5), static_cast<uint8_t>(6)})
        TEST_ASSERT_TRUE(DashLegacySteerDefense::isEngagedState(s));
    for (uint8_t s : {static_cast<uint8_t>(0), static_cast<uint8_t>(1),
                      static_cast<uint8_t>(2), static_cast<uint8_t>(7),
                      static_cast<uint8_t>(8), static_cast<uint8_t>(9),
                      static_cast<uint8_t>(15)})
        TEST_ASSERT_FALSE(DashLegacySteerDefense::isEngagedState(s));
}

void test_startup_engaged_is_baseline_not_edge()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(2000);
    d.observe(3, 100);
    DashLegacySteerDiag diag = d.diag(100);
    TEST_ASSERT_TRUE(diag.apEngaged);
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_EQUAL_UINT32(0, diag.apEdgeCount);
    DashLegacySteerDecision dec = d.decide(100);
    TEST_ASSERT_TRUE(dec.engaged);
    TEST_ASSERT_FALSE(dec.genuineEdge);
    TEST_ASSERT_FALSE(dec.instantBypass);
    TEST_ASSERT_FALSE(dec.allowed);
}

void test_real_edge_sets_edgePending_and_counts()
{
    DashLegacySteerDefense d;
    d.observe(2, 100);
    d.observe(3, 200);
    DashLegacySteerDiag diag = d.diag(200);
    TEST_ASSERT_TRUE(diag.edgePending);
    TEST_ASSERT_EQUAL_UINT32(1, diag.apEdgeCount);
    TEST_ASSERT_EQUAL_UINT32(200, diag.lastApEdgeMs);
}

void test_instant_off_settle_waits_debounce()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(2000);
    d.setInstantEnabled(false);
    d.observe(2, 100);
    d.observe(3, 200);
    d.observe(3, 2199); // keep DAS fresh so this tests settle, not the freshness gate
    DashLegacySteerDecision before = d.decide(2199);
    TEST_ASSERT_FALSE(before.allowed);
    TEST_ASSERT_FALSE(before.instantBypass);
    d.observe(3, 2200); // keep DAS fresh so this tests settle, not the freshness gate
    DashLegacySteerDecision at = d.decide(2200);
    TEST_ASSERT_TRUE(at.debounceSatisfied);
    TEST_ASSERT_TRUE(at.allowed);
}

void test_instant_on_one_shot_edge_bypass()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(2000);
    d.setInstantEnabled(true);
    d.observe(2, 10);
    d.observe(3, 20);
    DashLegacySteerDecision first = d.decide(20);
    TEST_ASSERT_TRUE(first.genuineEdge);
    TEST_ASSERT_TRUE(first.instantBypass);
    TEST_ASSERT_TRUE(first.allowed);
    DashLegacySteerDecision second = d.decide(21);
    TEST_ASSERT_FALSE(second.genuineEdge);
    TEST_ASSERT_FALSE(second.instantBypass);
    TEST_ASSERT_FALSE(second.allowed);
    TEST_ASSERT_EQUAL_UINT32(1, d.diag(21).instantBypassCount);
    TEST_ASSERT_FALSE(d.diag(21).instantBypassLast); // second decide did not re-fire
}

void test_minimal_edge_burst_opens_5_then_stops()
{
    DashLegacySteerDefense d;
    d.setMinimalEnabled(true);
    d.observe(2, 100);
    d.observe(3, 200);
    for (uint8_t i = 0; i < DashLegacySteerDefense::kMinimalBudget; ++i)
    {
        DashLegacySteerDecision dec = d.decide(200 + i);
        TEST_ASSERT_TRUE(dec.minimalBurstOpen);
        TEST_ASSERT_TRUE(dec.allowed);
        TEST_ASSERT_TRUE(d.recordMinimalInjection("legacy_fsd_mux0"));
    }
    DashLegacySteerDecision after = d.decide(300);
    TEST_ASSERT_FALSE(after.minimalBurstOpen);
    TEST_ASSERT_FALSE(after.allowed);
    TEST_ASSERT_FALSE(d.recordMinimalInjection("legacy_fsd_mux0"));
    TEST_ASSERT_EQUAL_UINT32(1, d.diag(300).minimalBlocks);
    TEST_ASSERT_EQUAL_UINT8(DashLegacySteerDefense::kMinimalBudget, d.diag(300).minimalUsed);
}

void test_minimal_rearms_on_disengage_reengage()
{
    DashLegacySteerDefense d;
    d.setMinimalEnabled(true);
    d.observe(2, 100);
    d.observe(3, 200);
    for (uint8_t i = 0; i < DashLegacySteerDefense::kMinimalBudget; ++i)
    {
        d.decide(200 + i);
        d.recordMinimalInjection("p");
    }
    TEST_ASSERT_EQUAL_UINT8(DashLegacySteerDefense::kMinimalBudget, d.diag(300).minimalUsed);
    d.observe(1, 400); // disengage -> re-arm budget
    TEST_ASSERT_EQUAL_UINT8(0, d.diag(400).minimalUsed);
    d.observe(3, 500); // re-engage edge -> re-arm window
    DashLegacySteerDecision dec = d.decide(500);
    TEST_ASSERT_TRUE(dec.minimalBurstOpen);
    TEST_ASSERT_TRUE(dec.allowed);
}

void test_minimal_toggled_mid_episode_does_not_open()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(2000);
    d.observe(2, 100);
    d.observe(3, 200);         // edge while Minimal OFF -> window NOT armed
    d.decide(200);             // settle (debounce not satisfied)
    d.setMinimalEnabled(true); // toggle on mid-episode, no fresh edge
    DashLegacySteerDecision dec = d.decide(300);
    TEST_ASSERT_FALSE(dec.minimalBurstOpen);
    TEST_ASSERT_FALSE(dec.allowed);
    TEST_ASSERT_EQUAL_UINT8(0, d.diag(300).minimalUsed);
}

void test_minimal_exhaust_is_hard_stop_even_with_settle_satisfied()
{
    DashLegacySteerDefense d;
    d.setMinimalEnabled(true);
    d.setDebounceMs(100); // satisfied quickly
    d.observe(2, 100);
    d.observe(3, 200);
    for (uint8_t i = 0; i < DashLegacySteerDefense::kMinimalBudget; ++i)
    {
        DashLegacySteerDecision dec = d.decide(300 + i);
        TEST_ASSERT_TRUE(dec.debounceSatisfied);
        TEST_ASSERT_TRUE(dec.allowed); // minimal burst still open
        d.recordMinimalInjection("p");
    }
    DashLegacySteerDecision after = d.decide(500);
    TEST_ASSERT_TRUE(after.debounceSatisfied);
    TEST_ASSERT_FALSE(after.allowed); // minimal exhausted; settle does NOT rescue
}

void test_instant_and_minimal_both_on_minimal_governs()
{
    DashLegacySteerDefense d;
    d.setInstantEnabled(true);
    d.setMinimalEnabled(true);
    d.setDebounceMs(2000);
    d.observe(2, 100);
    d.observe(3, 200);
    for (uint8_t i = 0; i < DashLegacySteerDefense::kMinimalBudget; ++i)
    {
        DashLegacySteerDecision dec = d.decide(200 + i);
        TEST_ASSERT_FALSE(dec.instantBypass); // Minimal governs; Instant not fired
        TEST_ASSERT_TRUE(dec.minimalBurstOpen);
        TEST_ASSERT_TRUE(dec.allowed);
        d.recordMinimalInjection("p");
    }
    DashLegacySteerDecision after = d.decide(300);
    TEST_ASSERT_FALSE(after.allowed);
    TEST_ASSERT_EQUAL_UINT32(0, d.diag(300).instantBypassCount); // Instant never fired
}

void test_state8_and_9_clear_edge_and_disarm_burst()
{
    for (uint8_t s : {static_cast<uint8_t>(8), static_cast<uint8_t>(9)})
    {
        DashLegacySteerDefense d;
        d.setMinimalEnabled(true);
        d.observe(2, 100);
        d.observe(3, 200); // edge -> arm window
        d.decide(200);
        d.recordMinimalInjection("p");
        TEST_ASSERT_EQUAL_UINT8(1, d.diag(200).minimalUsed);
        d.observe(s, 300); // abort/fault -> non-engaged
        DashLegacySteerDiag diag = d.diag(300);
        TEST_ASSERT_FALSE(diag.apEngaged);
        TEST_ASSERT_FALSE(diag.edgePending);
        TEST_ASSERT_EQUAL_UINT8(0, diag.minimalUsed); // burst disarmed + budget reset
        DashLegacySteerDecision dec = d.decide(300);
        TEST_ASSERT_FALSE(dec.allowed);
    }
}

void test_clear_timing_clears_transient_keeps_counters()
{
    DashLegacySteerDefense d;
    d.setInstantEnabled(true);
    d.setDebounceMs(2000);
    d.observe(2, 100);
    d.observe(3, 200);
    d.decide(200); // Instant fires -> instantBypassCount=1
    TEST_ASSERT_EQUAL_UINT32(1, d.diag(200).instantBypassCount);
    d.clearTiming();
    DashLegacySteerDiag diag = d.diag(250);
    TEST_ASSERT_TRUE(diag.apEngaged);
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_FALSE(diag.debounceSatisfied);
    TEST_ASSERT_EQUAL_UINT32(1, diag.apEdgeCount);        // cumulative kept
    TEST_ASSERT_EQUAL_UINT32(1, diag.instantBypassCount); // cumulative kept
}

void test_uint32_wrap_preserves_debounce()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(100);
    d.observe(2, UINT32_MAX - 100);
    d.observe(3, UINT32_MAX - 50);
    DashLegacySteerDecision before = d.decide(48);
    TEST_ASSERT_FALSE(before.debounceSatisfied);
    TEST_ASSERT_FALSE(before.allowed);
    DashLegacySteerDecision at = d.decide(49);
    TEST_ASSERT_TRUE(at.debounceSatisfied);
    TEST_ASSERT_TRUE(at.allowed);
}

void test_reset_runtime_clears_observation_keeps_counters()
{
    DashLegacySteerDefense d;
    d.setInstantEnabled(true);
    d.setDebounceMs(2000);
    d.observe(2, 100);
    d.observe(3, 200);
    d.decide(200); // apEdgeCount=1, instantBypassCount=1
    d.resetRuntime();
    DashLegacySteerDiag diag = d.diag(300);
    TEST_ASSERT_FALSE(diag.apEngaged);
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_EQUAL_UINT32(1, diag.apEdgeCount);        // kept
    TEST_ASSERT_EQUAL_UINT32(1, diag.instantBypassCount); // kept
    d.observe(3, 400);                                    // re-observe after reset -> startup-engaged, NOT an edge
    TEST_ASSERT_FALSE(d.decide(400).genuineEdge);
}

void test_edge_age_is_zero_without_edge_and_wrap_safe_after_edge()
{
    DashLegacySteerDefense d;
    DashLegacySteerDiag empty = d.diag(12345);
    TEST_ASSERT_EQUAL_UINT32(0, empty.hasApEdge ? dashAgeMs(12345, empty.lastApEdgeMs) : 0);
    d.observe(2, UINT32_MAX - 20);
    d.observe(3, UINT32_MAX - 10);
    DashLegacySteerDiag wrapped = d.diag(5);
    TEST_ASSERT_TRUE(wrapped.hasApEdge);
    TEST_ASSERT_EQUAL_UINT32(16, dashAgeMs(5, wrapped.lastApEdgeMs));
}

// --- DAS freshness gate (#108 hardening / #122 parity) ---------------------
// Fail-closed: while engaged, block activation injection when no primary 0x399
// DAS frame has been observed within kDasFreshMs. 0x399 normally cycles fast,
// so the gate is invisible in normal operation and only bites on a stale bus.

void test_das_fresh_allows_while_recent()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(0); // settle instantly so only freshness can block
    d.observe(3, 5000); // engaged + DAS fresh at t=5000
    for (uint32_t t = 5050; t <= 5999; t += 50)
    {
        TEST_ASSERT_TRUE(d.decide(t).allowed);
        TEST_ASSERT_TRUE(d.diag(t).dasFresh);
    }
    TEST_ASSERT_EQUAL_UINT32(0, d.diag(5999).staleBlocks);
}

void test_das_stale_blocks_after_window()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(0);
    d.observe(3, 1000); // lastDasSeenMs_=1000
    // Within window (<= 1000 ms): allowed.
    TEST_ASSERT_TRUE(d.decide(1500).allowed);
    TEST_ASSERT_TRUE(d.decide(2000).allowed); // boundary inclusive
    // 1 ms past the window: fail-closed, DAS not fresh.
    DashLegacySteerDecision stale = d.decide(2001);
    TEST_ASSERT_FALSE(stale.allowed);
    DashLegacySteerDiag diag = d.diag(2001);
    TEST_ASSERT_FALSE(diag.dasFresh);
    TEST_ASSERT_EQUAL_UINT32(1, diag.staleBlocks);
}

void test_das_stale_does_not_consume_edge()
{
    DashLegacySteerDefense d;
    d.setInstantEnabled(true);
    d.setDebounceMs(5000); // settle NOT satisfied here -> only the edge can allow
    d.observe(2, 100);
    d.observe(3, 200); // rising edge at t=200, edgePending_=true
    // DAS aged out (1300 - 200 = 1100 > 1000): blocked, Instant did NOT fire,
    // and the edge must survive so it can still trigger once DAS returns.
    DashLegacySteerDecision stale = d.decide(1300);
    TEST_ASSERT_FALSE(stale.allowed);
    TEST_ASSERT_FALSE(stale.instantBypass);
    TEST_ASSERT_TRUE(d.diag(1300).edgePending);
    // Re-feed primary DAS -> fresh -> Instant now consumes the preserved edge.
    d.observe(3, 1350); // engaged==engaged: no new edge, just refreshes timestamp
    DashLegacySteerDecision after = d.decide(1350);
    TEST_ASSERT_TRUE(after.allowed);
    TEST_ASSERT_TRUE(after.instantBypass);
    TEST_ASSERT_FALSE(d.diag(1350).edgePending);
    // Edge is one-shot: a later frame no longer bypasses debounce.
    TEST_ASSERT_FALSE(d.decide(1351).instantBypass);
}

void test_reset_runtime_clears_das_freshness_keeps_stale_blocks()
{
    DashLegacySteerDefense d;
    d.setDebounceMs(0);
    d.observe(3, 1000);
    d.decide(1000); // fresh, allowed (no stale block)
    d.decide(2001); // stale -> staleBlocks=1
    TEST_ASSERT_EQUAL_UINT32(1, d.diag(2001).staleBlocks);
    d.resetRuntime();
    DashLegacySteerDiag after = d.diag(2001);
    TEST_ASSERT_FALSE(after.hasDasSeen); // freshness observation cleared
    TEST_ASSERT_FALSE(after.dasFresh);
    TEST_ASSERT_EQUAL_UINT32(1, after.staleBlocks); // cumulative counter survives
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_engaged_state_matrix);
    RUN_TEST(test_startup_engaged_is_baseline_not_edge);
    RUN_TEST(test_real_edge_sets_edgePending_and_counts);
    RUN_TEST(test_instant_off_settle_waits_debounce);
    RUN_TEST(test_instant_on_one_shot_edge_bypass);
    RUN_TEST(test_minimal_edge_burst_opens_5_then_stops);
    RUN_TEST(test_minimal_rearms_on_disengage_reengage);
    RUN_TEST(test_minimal_toggled_mid_episode_does_not_open);
    RUN_TEST(test_minimal_exhaust_is_hard_stop_even_with_settle_satisfied);
    RUN_TEST(test_instant_and_minimal_both_on_minimal_governs);
    RUN_TEST(test_state8_and_9_clear_edge_and_disarm_burst);
    RUN_TEST(test_clear_timing_clears_transient_keeps_counters);
    RUN_TEST(test_uint32_wrap_preserves_debounce);
    RUN_TEST(test_reset_runtime_clears_observation_keeps_counters);
    RUN_TEST(test_edge_age_is_zero_without_edge_and_wrap_safe_after_edge);
    RUN_TEST(test_das_fresh_allows_while_recent);
    RUN_TEST(test_das_stale_blocks_after_window);
    RUN_TEST(test_das_stale_does_not_consume_edge);
    RUN_TEST(test_reset_runtime_clears_das_freshness_keeps_stale_blocks);
    return UNITY_END();
}
