# NagHandler Real-Data Bench Validation — Design

**Date:** 2026-06-24
**Author:** ziwind (for Jordan)
**Status:** Design — awaiting review
**Related:** `docs/superpowers/specs/2026-06-23-steer-jerk-ap-injection-fix-design.md`; upstream EPAS-nag incident (2026-06-19, removed `DashEpasNagEngine`)
**Safety class:** Safety-relevant (steering), but **bench-only / no vehicle**. No new injection. EPAS-nag ban (no 0x370 revival) preserved.

---

## 1. Motivation

Jordan captured real `0x370` (EPAS3P_sysStatus) torque frames from a steering-wheel
"hold the wheel" nag event on his vehicle (`/Users/ziwind/Codex/DouyinFSD /抓包握方向盘警告后扭矩数据 2.txt`),
plus a public-source reference firmware (`/Users/ziwind/Codex/DouyinFSD /源代码与抓包数据/公开源代码.txt`)
whose `web_dnd_steer` injects 0x370. Jordan asked to explore a legacy steering-wheel
nag-killer.

The public source's `web_dnd_steer` (lines 1855–1910) is the **same hazard class** as the
removed `DashEpasNagEngine`: it tampers torque, force-sets the handsOn bit
(`data[4] |= 0x40`), and bumps the rolling counter (`data[6] = (counter+1)%16`). The
2026-06-19 incident root cause was: *a second transmitter on EPAS's own-status 0x370 →
EPAS cross-validation flags counter conflict + torque semantic contradiction → fault
protection → vehicle control disabled*. Building a new 0x370 injection engine is
**rejected** (would revive the banned hazard).

Instead Jordan chose the controlled path: **bench-validate the existing legacy `NagHandler`
with the real captured data**, on the desk, no car. This spec defines that validation.

## 2. Background — NagHandler's status

- `NagHandler` lives at `include/handlers.h:735-877` (`struct NagHandler : CarManagerBase`).
- It is the **legacy** 0x370 counter+1 echo method, kept as a baseline. The EPAS-nag guard
  contract (`test/test_no_epas_nag_contract.py:16`) **explicitly exempts** `NagHandler`
  from the 8 banned symbols — enabling/testing it does **not** trip the ban guard.
- Compile-gated by `-DNAG_KILLER` (`include/app.h:35-36`). **OFF** in the waveshare
  production profile (`platformio.ini:11-39`); ON only in native test envs
  (`platformio.ini:63,69,88`).
- Runtime-gated by `nagKillerActive` && `nagKillerRuntime` && `checkAD()`
  (`handlers.h:783-786`).
- Echo algorithm (`handlers.h:791-829`):
  - `data[0],[1],[5]` — passthrough.
  - `data[2] = (in.data[2] & 0xF0) | 0x08` (sign nibble forced positive).
  - `data[3] = 0xB6` — fixed `torsionBarTorque` = **1.80 Nm** (legacy path); an optional
    bionic sine path perturbs bytes 2/3.
  - `data[4] = in.data[4] | 0x40` — handsOnLevel = 1.
  - `data[6]` low nibble = `(in + 1) % 16` (counter + 1), upper nibble preserved.
  - `data[7] = (sum(data[0..6]) + 0x73) & 0xFF` — checksum (matches Tesla style:
    `(0x70 + 0x03) + sum = sum + 0x73`).
- Trigger: only echoes when `handsOn = (in.data[4] >> 6) & 0x03 == 0`.
- Existing test `test/test_native_nag/test_nag_handler.cpp` (26 tests) covers the
  mechanism but uses **synthetic** frames (`makeEpasFrame`). It has never processed the
  real capture.

## 3. Goal & honest scope limit

**Goal.** Feed Jordan's **real** captured 0x370 frames through `NagHandler` and produce
two kinds of evidence: (a) the echo is well-formed on real data; (b) data on the
counter-collision question that caused the 2026-06-19 fault.

**Scope limit (must be stated plainly).** A bench has **no real EPAS** transmitting, so
the bench **cannot** reproduce or rule out the on-vehicle fault. The codebase's own guard
docstring (`test_no_epas_nag_contract.py:52`) already states *"0x370 echo injection; it
faults the power-steering ECU."* Therefore:

- The bench **proves**: echo frame correctness (counter/checksum/torque/handsOn) against
  real bytes, and reports whether the +1 echo collides with the **observed** real counter
  stride.
- The bench **does not prove**: on-vehicle safety. The torque/handsOn **semantic
  contradiction** vector (echo claims handsOn=1 + 1.80 Nm while the real frame claims
  handsOn=0 + ~0 Nm) — the *other* half of the 2026-06-19 diagnosis — can only be flagged
  by a real EPAS, which we do not connect.

**Non-goals.** No new 0x370 injection engine; no lift of the EPAS ban; no change to the
waveshare production profile (NAG_KILLER stays OFF); no vehicle test; no decision yet on
whether NagHandler is ever used on the car (that decision waits for the bench output).

## 4. Data source

Decode real 0x370 frames from the capture txt (each line prints 8 data bytes in HEX, e.g.
`52 08 08 0F 20 2C 25 55`). Two warning events are captured (warning-active hands-off
phase, then a driver-dismissal torque spike). Observed facts in the capture:

- `data[6]` low nibble (counter) sequence: `5, 7, 9, B, D, 0, …` — **stride +2** (could be
  a sampling artifact of the monitoring firmware; the bench will measure the real stride
  distribution rather than assume it).
- `data[4]` bits[7:6] (`handsOn`) **= 0 throughout**, including during the driver's manual
  dismissal. This is a red flag (see §7, expected finding F2) — NagHandler's trigger
  signal may be inert on this vehicle, while the public source reads the nag level from
  **0x399 byte5** (`(data[5]>>2)&0x0F; >=3` = warning, public source line 1934), not
  0x370 byte4.
- Dismissal torque peaks at −1.76 Nm / +1.68 Nm; NagHandler's fixed 0xB6 = 1.80 Nm lands
  right at that threshold.

Take a **contiguous, counter-preserving sample** spanning both events and the full counter
range (≈60–100 frames). Decode at implementation time (programmatic, not hand-copied) into
a new header `test/test_native_nag/real_epas_frames.h`:

```cpp
struct RealEpasSample { CanFrame frame; bool expectEcho; const char* tag; };
static const RealEpasSample kRealEpasFrames[] = { /* decoded */ };
static const size_t kRealEpasFrameCount = sizeof(kRealEpasFrames)/sizeof(kRealEpasFrames[0]);
```

`expectEcho` = `((frame.data[4]>>6)&0x03)==0` (precomputed from the real byte; expected
true for all captured frames per the observation above).

## 5. New tests (appended after the existing 26)

All added to `test/test_native_nag/test_nag_handler.cpp`, guarded so they only compile
under `[env:native_nag]` (where `NAG_KILLER` is defined and `NagHandler` is selected).

**T1 — `test_nag_real_frames_echo_wellformed`**
Feed every real frame; for each emitted echo assert: valid checksum
(`sum(b0..6)+0x73`), `counter == (in+1)%16`, `(data[4]>>6)&0x03 == 1`, `data[3] == 0xB6`
(legacy 1.80 Nm path; bionic off in this test).

**T2 — `test_nag_real_echo_count_matches_handson0`**
`mock.sent.size() == count(kRealEpasFrames where expectEcho)`. Verifies the handsOn gate
fires on real bytes (and surfaces if `expectEcho` is true for *all* frames — see F2).

**T3 — `test_nag_real_counter_interleave_analysis`  (core)**
Simulate the bus timeline: real frames arrive in capture order; after each, NagHandler
emits its echo. Build the merged counter stream and:
- compute the real sequence's per-frame counter-delta distribution;
- count **collisions** = a real frame and an echo sharing a counter value where ordering
  would let EPAS see a duplicate/stale counter;
- `TEST_PRINTF` the stride distribution and collision count.
Assertion policy: assert the analysis **runs and reports**; if the measured stride is
uniformly +2, additionally assert collision count == 0 (echo +1 slots into odd counters).
Otherwise assert nothing numeric — just print the finding. This converts the fault
mechanism from speculation to measurement.

**T4 — `test_nag_real_handson_signal_sanity`**
Count how many real frames have `(data[4]>>6)&0x03 != 0`. `TEST_PRINTF` the result.
**Expected: 0** — i.e. NagHandler's 0x370-byte4 trigger never toggles on this vehicle,
so the component cannot selectively detect "hands returned." This is surfaced, not hidden.

A diagnostic dump (behind a `NAG_REAL_DATA_VERBOSE` compile flag, off by default) prints a
sampled input→echo trace for eyeballing.

## 6. File changes

| File | Action | Notes |
|---|---|---|
| `test/test_native_nag/real_epas_frames.h` | **new** | decoded real frames + metadata |
| `test/test_native_nag/test_nag_handler.cpp` | **extend** | `#include` the header; add T1–T4 + `RUN_TEST`; optional verbose flag |
| `platformio.ini` / `[env:native_nag]` | **none** | already has `-DNAG_KILLER` |
| `include/handlers.h`, `app.h`, waveshare profile | **none** | NagHandler unchanged |

## 7. Expected findings & what they mean

- **F1 (T3).** If the real counter stride is uniformly +2 and echo is +1 → **0 counter
  collisions** in the interleaved timeline. Interpretation: the *counter-conflict* vector
  of the 2026-06-19 fault would not arise from NagHandler on this stride — but the
  *torque/handsOn semantic-contradiction* vector remains untested (needs a real EPAS).
  Conclusion: "no counter collision" ≠ "safe." Honest framing only.
- **F2 (T4).** 0x370 byte4 handsOn is expected stuck at 0 → NagHandler's trigger is
  effectively "always echo" and cannot detect hands returning. The authoritative nag
  signal is 0x399 byte5 (per public source). This questions whether NagHandler is even
  the right component for Jordan's vehicle, independent of the EPAS-fault risk.

These two findings are the bench's real deliverable and directly inform the *next*
decision (which is out of scope here): keep exploring NagHandler, or pivot to the safe
0x3C2 wheel-button DND auto-driven by 0x399 (the path deferred from today's earlier
discussion).

## 8. Safety invariants (pinned at top of the test file)

1. EPAS-nag ban ⛔ intact: the 8 guarded symbols stay at 0 hits; no new 0x370 engine.
2. `NagHandler` remains compile-gated (`NAG_KILLER`) and runtime-gated
   (`nagKillerActive`/`nagKillerRuntime`); waveshare production build is unchanged
   (NAG_KILLER OFF).
3. Bench-only: no vehicle connection; no real EPAS on the bus.
4. Conclusions are limited to "echo well-formedness + counter evidence"; **not** an
   on-vehicle safety verdict.

## 9. Validation steps

1. `pio test -e native_nag` → 26 existing + 4 new = 30 pass; T3/T4 print their findings.
2. `pio test -e native_single_can_dashboard` → confirms NagHandler still compiles in the
   single-CAN dashboard context.
3. `python3 -m unittest discover -s test` → `test_no_epas_nag_contract.py` still green
   (NagHandler is exempt; verify no regression).
4. No production artifact rebuilt; no flash; no vehicle.

## 10. Open questions deferred

- Whether to proceed toward on-vehicle NagHandler (not in scope; waits on F1/F2 + a
  separate, explicit safety sign-off).
- Whether the legacy nag-killer should instead be the safe 0x3C2 DND auto-triggered by
  0x399 (deferred design; this bench's findings feed that decision).
