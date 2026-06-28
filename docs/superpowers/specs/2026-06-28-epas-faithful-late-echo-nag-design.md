# Waveshare Single-CAN EPAS-faithful Late Echo NAG Research Design

**Date:** 2026-06-28  
**Project:** `/Users/ziwind/my-vibe-project/waveshare-single-can-firmware`  
**Target branch:** `feature/human-replay-nag-v3`  
**Status:** Design draft for user review  
**Scope:** Single-CAN research mode only; no implementation in this spec step

---

## 1. Background

The local Waveshare single-CAN firmware previously implemented **TSL6P Burst NAG v4**:

- `0x399` is read-only and supplies DAS/AP state plus hands-on-state (HOS).
- `0x370` is the only transmitted NAG-related frame.
- Burst timing is `1000 ms ON / 1500 ms OFF`.
- Torque is bounded at `±180 raw` (`±1.80 Nm`).
- AP states `8/9` abort injection.
- The feature is opt-in and default-off.

Real-car test result after flashing the TSL6P v4 asset:

- FSD function was normal.
- Vehicle had no errors or warnings.
- NAG was still not suppressed.

Board-side diagnostics after the test showed the path was genuinely active:

```text
enabled=1
nagSamples=866
burstSessions=183
echoSent=4150
txFailures=0
abortBlocks=0
gateBlocks=0
[NVS] rn_ns=866 rn_rb=183 rn_pw=108 rn_es=4150
```

CSV analysis of `/Users/ziwind/Downloads/can_recording (34).csv` through `(43).csv` showed:

- `0x399` AP state low nibble was `6` whenever relevant.
- HOS `3` appeared in files `34`, `35`, `37`, and `40`.
- `0x370 T` frames appeared only when HOS was `3`.
- All transmitted `0x370` checksums were valid.
- The transmitted torque sequence decoded correctly as `+180,+150,-150,-180`.
- Burst timing matched the expected `~1s ON / ~1.5s OFF` pattern.

The decisive failure was counter/cadence topology:

```text
R(C) -> T(C+1) -> R(C+1)
```

The transmitted frame was sent `0~1 ms` after the previous real `0x370 R`, while the next real `0x370 R` arrived `36~44 ms` later with the same counter value as the injected frame. This happened for `162/162` transmitted frames.

Therefore the previous design was syntactically correct but likely ignored by the receiving ECU/DAS as an out-of-cadence duplicate-counter frame.

---

## 2. References

This design uses the following upstream/project references:

- hypery11/flipper-tesla-fsd `v2.16-beta.11`: EPAS-faithful direction; release notes indicate real EPAS may leave `0x370` `handsOnLevel` untouched and that modifying it was a bug.
- hypery11/flipper-tesla-fsd `v2.16-beta.10`: Nag Burst, Abort Guard, signal mapping, `~1s ON / ~1.5s OFF`, and `±1.8 Nm` torque cap.
- hypery11/flipper-tesla-fsd issue #122: 2026.14.x/2026.20 NAG discussion; highlights the need for `0x370` plus DAS HOS context (`0x399`/`0x39B`) and the limitations of single-CAN visibility.
- nicolozak/nag-killer: Mode B burst/pause and Mode C/Linu-style state machine with `apStateId`, `steeringId`, freshness gates, `|steeringAngle| <= 5°`, and `±1.80 Nm` cap.
- Local real-car CSV evidence: immediate `counter+1` echo collides with the next real `0x370` counter on Jordan's vehicle.

Reference URLs:

- <https://github.com/hypery11/flipper-tesla-fsd/releases/tag/v2.16-beta.11>
- <https://github.com/hypery11/flipper-tesla-fsd/releases/tag/v2.16-beta.10>
- <https://github.com/hypery11/flipper-tesla-fsd/issues/122>
- <https://gitlab.com/nicolozak/nag-killer>
- <https://zread.ai/hypery11/flipper-tesla-fsd/7-nag-killer-and-isa-chime-suppression>

---

## 3. Problem Statement

The previous TSL6P v4 implementation sends `0x370` immediately after receiving a real `0x370` frame:

```text
real 0x370 R(C) at t0
injected 0x370 T(C+1) at t0 + 0~1ms
real 0x370 R(C+1) at t0 + 36~44ms
```

For Jordan's vehicle, the real `0x370` counter already increments by `+1` every normal period. An immediate `counter+1` echo therefore creates a repeated next counter and appears far earlier than the normal 40 ms cadence.

The receiving side likely ignores or de-weights this injected frame. Increasing burst length, changing probability, or adjusting the old zero-mean torque table does not address this root cause.

---

## 4. Design Goals

### 4.1 Primary Goal

Design a new opt-in single-CAN research mode:

```text
EPAS-faithful Late Echo Mode B
```

The mode will move `0x370` echo timing from immediate echo to predicted late echo:

```text
Old:
R(C) -> 0~1ms T(C+1) -> 40ms R(C+1)

Target:
R(C) -> 35~38ms T(C+1) -> 40ms R(C+1)
```

The intent is to make the injected frame appear near the next real `0x370` slot, a few milliseconds before the real next frame, instead of as an anomalously early duplicate.

### 4.2 Secondary Goals

- Adopt the EPAS-faithful payload direction: preserve `0x370` `byte4` / `handsOnLevel` instead of forcing `handsOnLevel=1`.
- Retain Nag Burst rhythm: `1000 ms ON / 1500 ms OFF`.
- Replace zero-mean four-frame torque cycling with a bounded same-direction hold/walk per burst.
- Maintain a hard torque cap of `±180 raw` (`±1.80 Nm`).
- Add enough diagnostics to determine whether timing, payload, or acceptance remains the limiting factor.

---

## 5. Non-Goals

This first design explicitly does **not** do the following:

- No dual-CAN architecture.
- No physical gateway / frame replacement architecture.
- No writes to `0x399`.
- No writes to `0x129`.
- No writes to `0x39B`.
- No full signal-map productization.
- No full Mode C / CAAE implementation.
- No restoration of the old `DashEpasNagEngine` path that previously caused a serious vehicle fault.
- No torque above `±1.80 Nm`.
- No default-on dangerous path.
- No push, tag, release, OTA, flashing, or real-car testing without explicit later authorization.

If future work needs dual-CAN, gateway replacement, full Mode C, or broader CAN writes, it must receive a separate design/spec review.

---

## 6. High-Level Architecture

Add one new pure-logic header:

```text
include/dash_epas_late_echo.h
```

It will contain focused internal units:

- `DashEpasCadenceTracker`
- `DashEpasBurstController`
- `DashEpasFaithfulEncoder`
- `DashLateEchoScheduler`
- `DashEpasLateEchoDiag`

Integration points:

```text
LegacyHandler
  0x399 -> lateEcho.onDasStatus(...)
  0x370 -> lateEcho.onEpasFrame(...)
  tick  -> lateEcho.buildDueFrame(...) + driver.send(...)
```

The existing `0x399` block remains read-only. The `0x370` receive block no longer sends immediately; it only updates cadence and schedules a pending late echo.

---

## 7. Data Flow

### 7.1 `0x399 DAS_status`

`0x399` remains read-only.

Extract:

```text
apState = frame.data[0] & 0x0F
hos     = (frame.data[5] >> 2) & 0x0F
```

Use it for:

- AP-active gate.
- Abort Guard.
- HOS / NAG trigger.
- Burst state-machine input.

Rules:

```text
AP state 3/4/5/6 -> eligible active state
AP state 8/9     -> abort cooldown
HOS > 2          -> NAG active
HOS <= 2         -> clear/cancel pending echo
```

Forbidden:

- No `driver.send(0x399)`.
- No mutation of any `0x399` byte.

### 7.2 `0x370 EPAS status`

When a real `0x370 R` frame arrives:

1. Update cadence/counter tracker.
2. If burst mode is not currently ON, cancel pending echo.
3. If cadence is unstable, cancel pending echo.
4. If a previous pending echo was not sent before this new `0x370`, count `lateWindowMissed` and cancel it.
5. If all gates pass, schedule a late echo near the predicted next slot.

Actual TX is performed only by the periodic/tick path when the scheduled time arrives.

---

## 8. Late Echo Scheduling

### 8.1 Formula

```text
periodMs = learned median 0x370 period
leadMs   = 3ms default
sendAt   = lastRxMs + periodMs - leadMs
```

Example:

```text
lastRxMs = 1000
periodMs = 40
leadMs   = 3
sendAt   = 1037
```

### 8.2 Send Window

A pending echo may only send if:

```text
nowMs >= sendAt
nowMs <= sendAt + maxLatenessMs
```

Recommended first value:

```text
maxLatenessMs = 1~2ms
```

If the window is missed, drop the pending echo. Do not send late.

### 8.3 New Source Frame Before TX

If a new real `0x370 R` arrives before the pending echo sends, the window was missed. Drop the pending echo and increment `lateWindowMissed`.

This prevents sending a stale counter after the real next frame has already appeared.

---

## 9. Cadence and Counter Gate

`DashEpasCadenceTracker` only observes real `0x370 R` frames.

Recommended initial constants:

```text
kMinStableSamples = 8
kMinPeriodMs      = 35
kMaxPeriodMs      = 45
kMaxJitterMs      = 5
kLateEchoLeadMs   = 3
kMaxEpasStaleMs   = 100
```

The tracker reports:

```text
stable
periodMs
jitterMs
counterStep
expectedNextCounter
predictedNextRxMs
lateEchoEligible
blockedReason
```

Failure reasons include:

```text
cadenceUnstable
counterUnstable
epasStale
lateWindowMissed
```

---

## 10. EPAS-faithful Payload

`DashEpasFaithfulEncoder` constructs the outgoing `0x370` frame.

### 10.1 Preserved Fields

```text
out.id   = 0x370
out.dlc  = 8
out[0]   = source[0]
out[1]   = source[1]
out[2].high_nibble = source[2].high_nibble
out[4]   = source[4]
out[5]   = source[5]
out[6].high_nibble = source[6].high_nibble
```

The important change is:

```text
out[4] = source[4]
```

The EPAS-faithful path must **not** do:

```text
(source[4] & 0x3F) | 0x40
```

### 10.2 Modified Fields

```text
out[2].low_nibble + out[3] = encoded bounded torque
out[6].low_nibble          = expectedNextCounter
out[7]                     = checksum
```

Checksum:

```text
(sum(out[0..6]) + 0x73) & 0xFF
```

### 10.3 Torque Encoding

Use the current signed raw encoding pattern:

```text
encoded = clamp(targetTorqueRaw, -180, 180) + 0x800
out[2].low_nibble = encoded >> 8
out[3] = encoded & 0xFF
```

Clamp in both the burst controller and encoder.

---

## 11. Torque Strategy

The first version should not reuse the zero-mean table:

```text
+180,+150,-150,-180
```

Instead use **Faithful Hold**:

- Each burst chooses one direction.
- Within a burst, hold/walk between `150..180 raw` in that direction.
- Alternate direction on the next burst to avoid long-term bias.

Example:

```text
Burst 1: +150, +160, +170, +180, +170, +160
Burst 2: -150, -160, -170, -180, -170, -160
```

Reasoning:

- The previous zero-mean pattern may be filtered out by DAS as noise.
- Same-direction hold produces low-frequency torque/integral closer to real hand force.
- Alternating burst direction limits persistent bias.

---

## 12. Safety Gates

A due echo may only be transmitted if all gates pass at the actual send moment.

### Gate 1: Explicit Opt-In

Required:

```text
canActive == true
lateEchoMode == enabled
user explicitly enabled the experimental NAG mode
```

Default: OFF.

Failure reason:

```text
toggle
```

### Gate 2: AP State

Allowed:

```text
3,4,5,6
```

Abort cooldown:

```text
8,9
```

Failure reasons:

```text
apInactive
abort
```

### Gate 3: HOS

Allowed:

```text
HOS > 2
```

Clear/cancel:

```text
HOS <= 2
```

Failure reason:

```text
hosClear
```

### Gate 4: Cadence / Counter

Required:

```text
stable 0x370 period
period around 35~45ms
jitter <= 5ms
counter progression understood
source frame fresh
```

Failure reasons:

```text
cadenceUnstable
counterUnstable
epasStale
```

### Gate 5: Timing Window

Required:

```text
now is within the scheduled late echo window
no newer source 0x370 has arrived before TX
```

Failure reason:

```text
lateWindowMissed
```

### Optional Future Gate: Steering Angle

First version only observes `0x129` if available; it does not require it for Mode B Late Echo.

A future Mode C design may require:

```text
0x129 fresh < 1000ms
|steeringAngle| <= 5°
```

---

## 13. Diagnostics

Diagnostics are required for the next real-car test to be interpretable.

### 13.1 `/status.reactiveNag`

Add fields similar to:

```json
{
  "lateEchoMode": true,
  "cadenceStable": true,
  "periodMs": 40,
  "jitterMs": 2,
  "counterStep": 1,
  "lateEchoEligible": true,
  "pendingEcho": false,
  "scheduledEchoes": 12,
  "sentLateEchoes": 10,
  "droppedLateEchoes": 2,
  "lateWindowMissed": 1,
  "lastRxToTxMs": 37,
  "lastLeadMs": 3,
  "preserveHandsOnLevel": true,
  "lastSourceHandsOnLevel": 0,
  "lastTxHandsOnLevel": 0,
  "lastTorqueRaw": 170,
  "blockedReason": "none"
}
```

### 13.2 Serial `reactive_nag`

Add a section:

```text
=== EPAS-faithful Late Echo ===
enabled=0 mode=IDLE pending=0
cadenceStable=1 periodMs=40 jitterMs=2 counterStep=1
lateEchoEligible=1
scheduled=0 sent=0 dropped=0 missed=0
lastRxToTxMs=37 leadMs=3
preserveHandsOnLevel=1 sourceHO=0 txHO=0
lastTorqueRaw=170 blockedReason=none
```

The most important next-test field is:

```text
lastRxToTxMs
```

It must move from the old `0~1ms` behavior to approximately `35~38ms`.

---

## 14. Dashboard / UX

Keep the UI minimal for the first version.

Expose:

```text
NAG Mode:
  Off
  EPAS-faithful Late Echo Experimental
```

Optionally show legacy TSL6P as deprecated/internal, but do not encourage it.

Do not expose many tuning knobs initially. Keep these fixed:

```text
leadMs=3
burstMs=1000
pauseMs=1500
torque profile=150..180 raw
```

Dashboard copy must say:

```text
Experimental. Default off. Test only in a safe controlled environment. The firmware fails closed when cadence, counter, AP, HOS, or timing gates are not satisfied.
```

---

## 15. Configuration / NVS

Use minimal persistent configuration:

```text
def_nag_mode
  0 = off
  1 = epas_late_echo
```

If legacy TSL6P is retained for comparison:

```text
def_nag_mode
  0 = off
  1 = legacy_tsl6p
  2 = epas_late_echo
```

Recommendation: only expose `off` and `epas_late_echo` in the user-facing UI for the first version.

---

## 16. Test Plan

### 16.1 Pure Native Tests

Add a focused native test suite, preferably:

```text
test/test_native_epas_late_echo/test_main.cpp
```

Required cases:

- Cadence becomes stable after 8 clean 40 ms frames.
- Jitter above threshold blocks eligibility.
- Counter jump blocks eligibility.
- Expected next counter is correct.
- `sendAt = lastRx + period - lead`.
- `due()` is true only inside the window.
- New `0x370 R` before due cancels pending and increments missed/drop diagnostics.
- Encoder preserves `byte4` exactly.
- Encoder does not OR `0x40` in EPAS-faithful mode.
- Encoder checksum is valid.
- Encoder clamps torque to `±180`.
- HOS clear cancels pending echo.
- AP state `8/9` enters cooldown and cancels pending echo.

### 16.2 LegacyHandler Integration Tests

Update:

```text
test/test_native_legacy/test_legacy_handler.cpp
```

Required cases:

- `0x399 HOS=3` plus stable `0x370` cadence schedules echo.
- Receiving `0x370` does **not** immediately send.
- Tick at `sendAt` sends exactly one `0x370`.
- Sent frame preserves `byte4`.
- Sent frame counter equals expected next counter.
- Sent checksum is valid.
- A new `0x370` before tick cancels pending and records `lateWindowMissed`.
- AP state `8/9` blocks and cancels pending echo.

Key regression lock:

```text
framesSent must not increase inside the 0x370 receive handler.
framesSent may increase only from the due/tick path.
```

### 16.3 Contract Tests

Update:

```text
test/test_no_epas_nag_contract.py
test/test_dashboard_api_contract.py
```

Contract requirements:

- `0x399` remains read-only.
- First version does not transmit or mutate `0x129`.
- `0x370` is the only NAG-related TX target.
- EPAS-faithful Late Echo preserves `byte4`.
- Torque cap remains `±180 raw`.
- There is an explicit scheduler/tick path.
- No immediate send occurs inside the `0x370` receive block for the new mode.

---

## 17. Verification Commands

The implementation plan should include at least:

```bash
pio test -e native_reactive_nag
pio test -e native_legacy
pio test -e native_single_can_dashboard
PYTHONPATH=test python3 -m unittest discover -s test -p 'test_*.py'
python3 scripts/minify_dashboard.py --check
pio run -e waveshare_single_can_standalone
```

If a new native env is added:

```bash
pio test -e native_epas_late_echo
```

---

## 18. Real-Car Verification Plan

No real-car testing is authorized by this design. If later authorized, judge results in layers.

### L0: Safety

Required:

```text
FSD normal
No vehicle errors
No warnings
No AP abort 8/9
No txFailures
No unexpected reset
```

### L1: Scheduler Effectiveness

CSV should change from:

```text
R(C) -> 0~1ms T(C+1) -> 40ms R(C+1)
```

to:

```text
R(C) -> 35~38ms T(C+1) -> 40ms R(C+1)
```

Board diagnostics should show:

```text
lastRxToTxMs ≈ 35~38
sentLateEchoes > 0
lateWindowMissed not continuously increasing
```

### L2: NAG Improvement

Observe:

```text
HOS=3 transitions to <=2
NAG prompt disappears or shortens
HOS=3 duration decreases
burstSessions decrease over time
```

### L3: Stability

Observe:

```text
No errors during curves
No abnormal AP disengage
No warnings during longer controlled testing
```

---

## 19. Success and Failure Criteria

### Success

The mode is considered promising if:

- L0 safety passes.
- L1 timing shifts to late echo.
- L2 NAG disappears or materially improves.

### Partial Success

If L0 and L1 pass but L2 fails:

- Counter/cadence was fixed.
- Payload or torque semantics remain ineffective.
- Next design discussion may consider Mode C/CAAE, steering-angle gate, or more advanced torque profiles.

### Failure

If L1 fails:

- Single-CAN timing may be too unreliable.
- Do not tune torque until scheduler timing is correct.

If any vehicle warning/error appears:

- Disable and remove the experimental path.
- Return to a safe firmware build.

---

## 20. Recommended Implementation Scope

First implementation should include only:

- Safe `0x370` cadence/counter detector.
- EPAS-faithful Late Echo Mode B.
- Minimal `def_nag_mode` persistence.
- Serial and dashboard diagnostics.
- Native/unit/integration/contract tests.

Do not implement full Mode C or configurable signal map in this first iteration.

---

## 21. Open Questions for Later Specs

These are intentionally deferred:

- Whether `0x129` is consistently visible on Jordan's current single-CAN tap.
- Whether Mode C/CAAE improves outcome after late echo timing is proven.
- Whether a physical gateway/replacement architecture is needed if single-CAN late echo still fails.
- Whether `0x39B` is relevant for future HW4 targets.
- Whether UI should expose advanced tuning knobs after the first controlled test.

---

## 22. Final Recommendation

Proceed with a first implementation plan for:

```text
Mode 0: Safe topology/cadence detector
Mode 1: EPAS-faithful Late Echo Mode B
```

Do not continue tuning the old immediate TSL6P v4 path as the main route. The local real-car evidence shows its immediate `counter+1` echo topology collides with the next real `0x370` counter on Jordan's vehicle.

The next implementation should answer one narrow question:

```text
Can a single-CAN device move 0x370 echo timing close enough to the next real frame slot for DAS to accept it, while preserving EPAS-faithful payload semantics and remaining fail-closed?
```
