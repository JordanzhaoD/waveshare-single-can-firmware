# Soft Engage (Activation-Edge Angle Gate) — Alignment with upstream v2.16-beta.10

**Date:** 2026-06-25 (rev. 3 — pure-helper extraction for native testability)
**Author:** ziwind (for Jordan)
**Status:** Design — awaiting review (rev. 3)
**Brainstorm track:** B (steer-jerk alignment). Track A (nag handsOnLevel) is a separate future spec.
**Related:** upstream `hypery11/flipper-tesla-fsd` release `v2.16-beta.10` ("Soft Engage + EPAS-faithful handsOnLevel fix"); local V1.0.3 steer-jerk fix (`memory: waveshare-single-can-v103-steer-jerk-fix-20260623`, commit `5e2d0db` / tag `v1.0.3-atlas-single-can`); local dual-mode NagHandler (`memory: waveshare-single-can-nag-dual-mode-refactor-20260624`).
**Safety class:** EPAS/steering-relevant (FSD activation edge). **Delivery scope: implement + bench-validate + flash + Jordan on-car test** (Jordan explicitly signed off on-car for this track — it is a refinement of the already-shipped V1.0.3 gate, not a new injection hazard class).

> **Revision 2 note.** Rev. 1 placed the gate at the HW3 mux-0 site (`handlers.h:630`). Planning found Jordan's car runs the **LegacyHandler** (`hwMode=0`); its `0x3EE bit46` gate is the dashboard static function `dashLegacyFsdActivationAllowed(nowMs)` (`mcp2515_dashboard.h:934`) — a **separate code path** that rev. 1 did not touch (i.e. rev. 1 would have had no effect on Jordan's car). Jordan approved moving the gate into the Legacy function. This also yields a **much simpler design**: the gate, its state, and the steering-angle source all live in the same dashboard translation unit, so no `Shared<bool>` runtime global, no `wheelCentred` callback, and no separate clock are required. The HW3/HW4 site (`handlers.h:630`) is **deferred (YAGNI)** — Jordan is on Legacy; noted in §9.

> **Revision 3 note.** Planning then found that `dashLegacyFsdActivationAllowed` lives in `mcp2515_dashboard.h`, whose entire body is guarded by `#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)` (`:3`) — all native test envs define `-DNATIVE_BUILD`, so the real gate is **not compiled natively** (it pulls in `<WebServer.h>`/`esp_wifi.h`). The existing V1.0.3 settle gate has the same limitation: handler tests exercise the gate via *stub callbacks*, never the real function. To give Soft Engage **real TDD coverage** without ESP hardware, the angle-gate decision is extracted into a **pure `inline` helper** `dashSoftEngageRelease(...)` in `can_helpers.h` (native-safe; same precedent as `enhancedAutopilotInjectionAllowed`). The dashboard gate calls the helper; the helper is unit-tested directly in `test_native_helpers`. Dashboard wiring (that the gate calls the helper + reads `apRestoreState` + manages the latch) is asserted by a Python contract test, mirroring `test_dashboard_api_contract.py`.

---

## 1. Background & decision chain

Upstream `v2.16-beta.10` shipped **Soft Engage** for the on-car steer-jerk:

> "holds the activation-edge injection until the wheel is near-centred, so the DAS's path recompute at FSD-enable is small (the jerk is worse on curves). Opt-in."

Local V1.0.3 already attacked the same jerk (upstream issues `flipper-tesla-fsd#108` / `ev-open-can-tools#66`, CN 2026.8.3.6) with a **state gate**: the Legacy `0x3EE bit46` injection is held off until AP has been continuously active for `legacyFsdRequiredStableMs` (~2 s). Data confirmed the jerk root cause = bit46 injected at the AP activation edge (+0.2s) → `0x488 DAS_steeringControl` jump Δ179–248° (normal Δ4°) → EPS jerk → AP fault-out. V1.0.3 reduced but did not eliminate it (<5% residual), and listed **edge-softening as future work**.

Upstream's Soft Engage IS that edge-softening, with a more precise condition: not "wait until AP stable" (a time/state gate) but **additionally** "wait until the **wheel is near-centred**" (an angle gate) — which directly explains why the jerk is worse on curves (large path-recompute when off-centre).

**Jordan's directive:** align V1.0.3 with upstream Soft Engage. Deliver **implement + bench-validate + flash + on-car test** (on-car signed off for this track). Track A (nag) deferred.

**Key data gap:** Jordan's jerk candumps (`scratch/steer-jerk/jerk-3.csv`, `jerk-4.csv`, `real/ids_3EE_399_488_118_145-*.txt`) do **not** contain `0x129` (steering angle) — the V1.0.3 diagnosis had no angle data. Therefore the "near-centred" threshold cannot be data-derived; it ships as a **conservative default, tunable on-car**.

## 2. Goal & honest scope limit

**Goal.** Add a "Soft Engage" angle gate to the Legacy `0x3EE bit46` FSD-activation path: at the activation edge, **additionally hold** the bit46 assertion until the steering wheel is near-centred (or a timeout fallback fires), then assert it and latch for the rest of the episode. This refines V1.0.3's time gate with the angle condition upstream identified, layered **on top of** (not replacing) the existing AP-settle gate.

**Scope limit (stated plainly).** A bench has no real EPAS/DAS and Jordan's captures lack `0x129`, so the bench verifies the **gate logic only** (angle>threshold holds, centred releases, latch persists, timeout releases, toggle/validity behave). It **cannot** measure real jerk reduction — that is validated on-car by Jordan. The threshold/timeout defaults are conservative guesses, tuned live.

## 3. Architecture — additive angle gate inside the Legacy activation function

### 3.1 Gate placement

The Legacy `0x3EE bit46` activation is gated by the dashboard static function `dashLegacyFsdActivationAllowed(uint32_t nowMs)` (`include/web/mcp2515_dashboard.h:934`). This is a callback installed onto the LegacyHandler via `dashHandler->legacyFsdActivationAllowed = dashLegacyFsdActivationAllowed;` (`:1322`, `:6703`). When it returns `false`, the handler does not assert bit46 (FSD not yet activated); the frame still passes through with stock bits. **This is Jordan's production path.**

Current function body (`:934-970`):

```cpp
static bool dashLegacyFsdActivationAllowed(uint32_t nowMs)
{
    if (!canActive)            { /* reset latch + blocked */ return false; }
    if (!dashOtaGuardAllowInjection()) { /* reset latch + blocked */ return false; }
    if (!apInjectionGate)      { legacyFsdLastAllowed = true; return true; } // bypass (gate OFF)
    bool apActive = dashHandler && (bool)dashHandler->APActive;
    if (!apActive)             { /* reset latch + blocked */ return false; }
    if (legacyFsdApActiveSinceMs == 0)
        legacyFsdApActiveSinceMs = nowMs;                          // arm settle timer
    bool stable = (nowMs - legacyFsdApActiveSinceMs) >= legacyFsdRequiredStableMs;
    legacyFsdLastAllowed = stable;
    if (!stable) legacyFsdLastBlockedMs = nowMs;
    return stable;
}
```

Soft Engage inserts an **angle condition between "stable" and "return true"**, plus an episode latch. Because the dashboard function is not compiled natively (see rev.3 note), the angle decision is a **pure `inline` helper** in `can_helpers.h`:

```cpp
// Pure Soft Engage angle-gate decision (native-testable). The dashboard gate
// dashLegacyFsdActivationAllowed() calls this once AP-settle is being evaluated.
// Returns true iff the Legacy 0x3EE bit46 activation may fire NOW; false means
// hold bit46 off until the wheel nears centre or the timeout elapses.
inline bool dashSoftEngageRelease(bool enabled, bool alreadySent,
                                  bool steerSeen, uint8_t steerValidity,
                                  int16_t steerAngleX10,
                                  bool settled, bool timeout,
                                  int angleThreshX10)
{
    if (!enabled)    return true;   // toggle OFF → V1.0.3 behaviour
    if (alreadySent) return true;   // already activated this episode (latched)
    if (!settled)    return true;   // settle gate hasn't passed yet (not our job)
    const bool centred = steerSeen
                         && steerValidity == 0
                         && abs(static_cast<int>(steerAngleX10)) <= angleThreshX10;
    return centred || timeout;
}
```

Revised tail of `dashLegacyFsdActivationAllowed` (the early-exit blocks above are unchanged):

```cpp
    if (legacyFsdApActiveSinceMs == 0) {
        legacyFsdApActiveSinceMs = nowMs;
        legacySoftEngageSent = false;            // new AP episode → re-arm soft-engage latch
    }
    const bool stable = (nowMs - legacyFsdApActiveSinceMs) >= legacyFsdRequiredStableMs;
    const bool timeout = (nowMs - legacyFsdApActiveSinceMs)
                         >= (legacyFsdRequiredStableMs + SOFT_ENGAGE_TIMEOUT_MS);
    const bool release = dashSoftEngageRelease(dashSoftEngage, legacySoftEngageSent,
                                               apRestoreState.steerSeen,
                                               apRestoreState.steerValidity,
                                               apRestoreState.steerAngleX10,
                                               stable, timeout, SOFT_ENGAGE_ANGLE_THRESH_X10);
    if (stable && !release) {
        legacyFsdLastAllowed = false;
        legacyFsdLastBlockedMs = nowMs;
        return false;                           // hold bit46 off: off-centre, within timeout window
    }
    if (stable) legacySoftEngageSent = true;     // latch: angle ignored for the rest of the episode
    legacyFsdLastAllowed = stable;
    if (!stable) legacyFsdLastBlockedMs = nowMs;
    return stable;
```

**Why here, not in the handler.** The handler holds `CarManagerBase*` only and cannot see `apRestoreState`; the dashboard owns the `0x129` decode and the settle clock. Putting the angle gate in the dashboard function keeps every Soft-Engage input — clock (`nowMs`), AP state (`dashHandler->APActive`), settle timer (`legacyFsdApActiveSinceMs`), and steering angle (`apRestoreState`) — in one translation unit. The pure helper is extracted only for testability; it carries no state and is a leaf call.

### 3.2 Why edge-latched (the core difference from V1.0.3)

Steering angle is **not monotonic**: once FSD is steering, the wheel legitimately goes off-centre (corners). So the angle gate MUST be edge-latched:
- `legacySoftEngageSent` latches `true` on the first bit46 assertion of an episode.
- It is reset to `false` only at the **re-arm of the settle timer** — i.e. when `legacyFsdApActiveSinceMs` returns to `0` (AP lost, CAN lost, OTA guard off, or gate bypass toggled) and AP next becomes active again. That is exactly one reset per AP episode.
- Without the latch, mid-corner off-centre angles would cut bit46 ⇒ a NEW hazard. V1.0.3's `APActive` gate works as a continuous state gate because `APActive` stays true for the whole episode; angle cannot.

### 3.3 Timeout fallback (on-car safety-critical)

`timeout = (nowMs - legacyFsdApActiveSinceMs) >= (legacyFsdRequiredStableMs + SOFT_ENGAGE_TIMEOUT_MS)`. This guarantees FSD still engages on a long curve where the wheel never centres — **the driver is never stranded waiting for centre**. The timeout is measured from AP-arm (same clock as settle), so total worst-case hold = settle (≈2 s) + `SOFT_ENGAGE_TIMEOUT_MS` (5 s) ≈ 7 s.

### 3.4 Steering-angle wiring (single-CAN, already parsed)

`0x129` (SAS / `DI_steeringAngle`) is on vehicle CAN-A and already decoded by the dashboard into `apRestoreState` (`mcp2515_dashboard.h:460-480`, parsed at `:685-693`):

```cpp
apRestoreState.steerSeen     = true;                              // :689
apRestoreState.steerValidity = (uint8_t)dashReadBitsLE(frame,30,2); // :690  bits 30-31
apRestoreState.steerAngleX10 = angleRaw == 0x3FFF ? 0 : (int16_t)angleRaw - 8192; // :691
```

The gate reads these directly. `centred` requires **all three**: `steerSeen` (we have seen `0x129`), `steerValidity == 0` (signal valid — see §8 risk), and `|steerAngleX10| ≤ threshold`. If `0x129` is unseen or validity ≠ 0 ⇒ `centred` false ⇒ the **timeout** path releases (never blocks indefinitely on unknown angle). No callback, no handler change.

### 3.5 Clock (testable, reused)

`nowMs` is the function parameter — `millis()` in firmware, `dashDiagNowMs()` in native tests (per the existing comment at `:931-933`). Native tests advance `dashDiagNowMs()` directly to control both the settle and the timeout. **No new clock, no thread, no ISR.**

## 4. File changes

| File | Action | Notes |
|---|---|---|
| `include/can_helpers.h` | **modify** — add pure `inline bool dashSoftEngageRelease(...)` (signature in §3.1) near `enhancedAutopilotInjectionAllowed` (`:46`) | **native-testable decision** |
| `include/web/mcp2515_dashboard.h` | **modify** — (a) add constants `SOFT_ENGAGE_ANGLE_THRESH_X10` (50), `SOFT_ENGAGE_TIMEOUT_MS` (5000), `kSoftEngageDefaultEnabled` (true) near `:172`; (b) add file-scope statics `dashSoftEngage{kSoftEngageDefaultEnabled}`, `legacySoftEngageSent{false}` near `:175-178`; (c) call `dashSoftEngageRelease(...)` + latch reset inside `dashLegacyFsdActivationAllowed` (`:963-969`); (d) NVS load `def_se` in `dashLoadPrefs` (`:1554`); (e) `/defense_config` GET adds `soft_engage`, POST reads `soft_engage`; (f) `/status` adds `softEngage` | **core gate + opt-in surface** |
| `include/web/mcp2515_dashboard_ui.src.h` | **modify** — add `def-soft-engage-tgl` checkbox under the AP-First gate card; wire `loadDefenseConfig`/`saveDefenseConfig`; regen minified `ui.h` via `python3 scripts/minify_dashboard.py` | UI |
| `test/test_native_helpers/test_helpers.cpp` | **add** — `test_dashSoftEngageRelease_*` cases (§7) exercising the pure helper directly | TDD (native) |
| `test/test_dashboard_api_contract.py` | **add** — assertions that `dashLegacyFsdActivationAllowed` calls `dashSoftEngageRelease`, reads `apRestoreState.steer*`, and manages `legacySoftEngageSent`; + `def_se` NVS + `soft_engage`/`softEngage` HTTP fields exist | wiring contract |
| `include/handlers.h` | **none** | angle gate is dashboard-only |
| `platformio.ini` / waveshare profile | **none** | rides existing `INJECTION_AFTER_AP` + native envs |

## 5. Defaults & configurability

| Setting | Default | Tunable | Rationale |
|---|---|---|---|
| Soft Engage enabled | **true** (ON) | UI toggle, NVS `def_se` | Jordan is the 8.3.6 jerk owner — wants the fix active; OFF reverts to V1.0.3 behaviour |
| `SOFT_ENGAGE_ANGLE_THRESH_X10` | **50** (±5°) | compile-time (on-car tune via rebuild) | conservative; Jordan's data lacks `0x129` so not data-derived |
| `SOFT_ENGAGE_TIMEOUT_MS` | **5000** (5 s) | compile-time | long-curve fallback; never strand the driver |
| Relation to AP-settle gate | **AND** (settle first, then angle) | — | preserves V1.0.3 safe baseline; angle gate only evaluates once settle passes |

UI: a sub-toggle "Soft Engage 方向盘居中" under the existing `ap-core-gate-tgl` (AP-First) card, desc: "激活时 hold 到方向盘近居中再注入，降弯道猛甩（超时 5s 兜底）". Wired through the proven `saveDefenseConfig()` → POST `/defense_config` path (parallel to `def-ntt-tgl`).

## 6. Safety invariants (pinned at the gate)

1. **No new injection.** Soft Engage only *delays* an existing bit46 assertion; it never injects anything V1.0.3 didn't. Worst case (toggle ON, wheel never centres, timeout fires) = exactly V1.0.3 behaviour after the timeout.
2. **Edge-latched.** Once bit46 is allowed in an episode, angle is ignored until the next episode — mid-corner off-centre cannot cut activation.
3. **Timeout-bounded.** Hold ≤ settle + `SOFT_ENGAGE_TIMEOUT_MS`; driver is never stranded.
4. **Unknown-angle-safe.** Invalid/unseen `0x129` ⇒ `centred` false ⇒ timeout releases (no indefinite hold on missing data).
5. **Bypassable.** Toggle OFF ⇒ the angle block is skipped entirely ⇒ bit46 fires exactly as V1.0.3 (settle-gated).
6. **No EPAS interaction.** Entirely `0x3EE`-side. Does not touch `0x370`/EPAS; the 8 banned `DashEpasNag` symbols and the EPAS guard (`test_no_epas_nag_contract.py`) are unaffected.

## 7. Testing

- **Unit (new, TDD — pure helper, `native` env via `test_native_helpers`):** `test_dashSoftEngageRelease_*` call `dashSoftEngageRelease(...)` directly (no handler, no dashboard header) — (a) `enabled=false` ⇒ true (toggle-OFF bypass) regardless of angle; (b) `alreadySent=true` ⇒ true (latch) regardless of angle; (c) `settled=false` ⇒ true (settle gate handles it) even when off-centre; (d) `settled=true` + centred (`steerSeen`, `validity=0`, `angle=0`) + `timeout=false` ⇒ true; (e) `settled=true` + off-centre (`angle=100>50`) + `timeout=false` ⇒ **false** (the hold); (f) `settled=true` + off-centre + `timeout=true` ⇒ true (timeout fallback); (g) `settled=true` + `steerSeen=false` + `timeout=false` ⇒ false (unknown angle holds); (h) `settled=true` + `validity=1` (invalid) + centred angle + `timeout=false` ⇒ false; (i) boundary `angle==50` ⇒ true (`≤` inclusive).
- **Contract (Python, `test_dashboard_api_contract.py`):** assert (1) `dashLegacyFsdActivationAllowed` source calls `dashSoftEngageRelease`; (2) it reads `apRestoreState.steerSeen`/`steerValidity`/`steerAngleX10`; (3) `legacySoftEngageSent` is declared and reset on `legacyFsdApActiveSinceMs == 0`; (4) NVS key `def_se`, `/defense_config` field `soft_engage`, `/status` field `softEngage` exist; (5) `dashSoftEngageRelease` is defined in `can_helpers.h`.
- **Regression:** existing `native_injection_after_ap` handler tests are **unaffected** — they use stub callbacks (`legacyGateAlwaysAllow`, etc.) and never touch `dashSoftEngage`/`apRestoreState`, so default-ON has no effect on them. (The dashboard function's default-ON only matters on the ESP build, validated on-car.)
- **Build:** `pio run -e waveshare_single_can_standalone` SUCCESS.
- **Guard:** `test_no_epas_nag_contract.py` unchanged and green (this work adds no EPAS symbols).
- **Bench cannot measure real jerk** (no `0x129` in captures) — only gate logic.
- **On-car (Jordan):** flash merged-flash asset → (1) straight road: FSD engages normally (wheel already centred, no extra hold); (2) curve: brief hold then engage on straightening, or engage at timeout; (3) tune threshold/timeout to best feel. Report back to adjust defaults.

## 8. Open risks (stated, not resolved)

- **Threshold is a guess.** ±5° is conservative; the real "small path-recompute" threshold is unknown without a `0x129` capture of a jerk event. On-car tune required; may need a new capture including `0x129`.
- **`steerValidity` encoding.** The codebase decodes bits 30-31 but does not document which value means "valid". The gate assumes `0 == valid` (common SAS convention). If the car reports a different "valid" value, the gate holds until timeout (safe, but means a 5 s engage delay) — verify on the first drive; adjust the comparison if needed.
- **Timeout feel.** 5 s is a placeholder; too short ⇒ barely holds on curves, too long ⇒ noticeable FSD engage delay. On-car dial-in.
- **Default-ON vs ESP behaviour.** Soft Engage defaults ON in the dashboard gate (ESP build). The native handler tests are **unaffected** because they drive stub callbacks, not the real gate; the pure-helper tests assert the decision directly. The only place default-ON changes observable behaviour is the ESP firmware → validated on-car by Jordan.
- **0x129 bus presence.** Assumed on CAN-A (single-CAN reachable) because the dashboard already parses it; confirmed at runtime on-car (if `steerSeen` never sets, the timeout path keeps things safe).

## 9. Out of scope / deferred

- **HW3/HW4 site (`handlers.h:630` mux-0 block).** Jordan's car runs LegacyHandler; the HW3 angle gate is not exercised. Deferred (YAGNI) — a future spec can mirror this gate into the HW3 path if Jordan moves to an HW3 build.
- Track A (nag `handsOnLevel`-untouched + `0x399 das_hands_on_state` re-keying) — separate spec.
- Upstream beta.10's other items (HW4 Highland byte0 fallback #116, Continuous AP + brake-state #120, M5 button, Party-CAN nag guidance).
- Adaptive/self-learning threshold or timeout — fixed tunable defaults only.
- Data-backed threshold — would need a new `0x129`-inclusive jerk capture; future work.
