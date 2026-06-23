# Steer-Jerk on AP Injection — Root Cause & Fix Design

**Date:** 2026-06-23
**Author:** ziwind (for Jordan)
**Status:** Design — awaiting review
**Related:** upstream `hypery11/flipper-tesla-fsd#108`, `ev-open-can-tools#66`
**Safety class:** Safety-relevant (steering). No new injection introduced. EPAS-nag ban (no 0x370) preserved.

---

## 1. Problem

On a 2020 Tesla Model X (China, HW3 ATOM, factory firmware **2026.8.3.6**), activating
FSD via CAN injection right after engaging Autopilot produces a **violent steering-wheel
jerk** plus an on-screen system fault, and AP aborts. A remote reporter (`dunckencn`,
upstream issue #108) supplied three candump captures filtered to `0x3EE/0x399/0x488/0x118/0x145`:

- `ids_3EE_399_488_118_145-normal.txt` — AP engages, holds ~9 s, **no jerk** (reference).
- `ids_3EE_399_488_118_145-steerjerkerror-4.txt` — jerk ~4 s.
- `ids_3EE_399_488_118_145-steerjerkerror-16.txt` — jerk ~16 s.

The reporter also noted: ev-open-can-tools' "start-after-AP" succeeds ~95 %, jerk <5 %;
the <5 % residual is what these two jerk logs capture.

## 2. Evidence (data-driven)

`0x488` is `DAS_steeringControl` (the steering *command* AP issues). Its bytes 0–1 decode
as **steering angle × 10** (signed). Replaying the three logs (`scratch/steer-jerk/`):

| Metric | normal | jerk-4 | jerk-16 |
|---|---|---|---|
| 0x399 AP engage (→6) | 5.35 s | 3.94 s | 16.03 s |
| 0x488 max step (raw → deg) | 41 (Δ4°) | **2483 (Δ248°)** | **1793 (Δ179°)** |
| Spike time rel. engage | — | **+0.26 s** | **+0.20 s** |
| 0x399 fault (→8/9) | none, holds 9 s | 4.67 s (+0.74 s) | 16.46 s (+0.43 s) |

The raw step magnitudes (2483, 1793) match the reporter's肉眼 readings (Δ248°, Δ179°)
to within rounding — confirming the decode. In *normal*, the steering command stays within
Δ4°; in both jerk logs it spikes 40–60× larger **within ~0.2 s of AP engagement**, and AP
faults 0.2–0.5 s after the spike.

Note: `bit46` of `0x3EE` (the FSD-enable bit) is **never observed set** in any of the
three logs, including *normal* where FSD did activate. The captures are RX-only; the
device's TX (the injected `0x3EE bit46=1`) is not echoed into the stream. So injection
is invisible in these logs — but the **car's response** (`0x399` fault + `0x488` spike) is
visible, and that is what we diagnose from.

## 3. Root cause

**Activation-edge injection.** Injecting the FSD-enable step (`0x3EE` mux0 `bit46`
0→1) inside the ~0.2 s window where AP takes over steering disrupts the takeover
handshake on China 2026.8.3.6 (whose autonomy preflight is tightened — see #108:
another user lost FSD entirely after this update). AP then issues a wild steering
command (`0x488` spike) → EPS jerk → AP fault-disengages. This matches the upstream
owner's independent conclusion ("activation-edge transient").

Two defects in **our** firmware amplify it on this exact car:

- **D1 — `isDASAutopilotActive` does not recognize state 6.**
  `can_helpers.h:75` returns `status >= 3 && status <= 5`. This car engages to **state 6**,
  so `APActive` is permanently false. The AP-First settle gate, which keys off `APActive`,
  never arms — so even when the gate is ON, it cannot hold injection off the edge.

- **D2 — AP-First gate is OFF by default.**
  `mcp2515_dashboard.h:151-156` sets `DASH_AP_GATE_DEFAULT=true` only when
  `INJECTION_AFTER_AP` is defined at compile time. The waveshare build does not define it,
  so the gate is bypassed and Legacy `0x3EE` injection fires ungated — landing on the
  activation edge.

The gate machinery itself (toggle `apg` + delay `ap_delay_ms`, both NVS-persisted, both
exposed in the UI) **already exists and works**. The fix is to make it actually take
effect, not to build anything new.

## 4. Fix design

### Change 1 — Recognize DAS state 6 as AP-active (D1)

`include/can_helpers.h:75`:
```c
inline bool isDASAutopilotActive(uint8_t status)
{
    // DAS_autopilotState (0x399 byte0 low nibble). AP is actively engaged on
    // states 3–5 (older firmware) and 6 (CN 2026.8.3.6 and newer). State 8
    // (handover/warning) and 9 (fault) are NOT active.
    return status >= 3 && status <= 6;
}
```

Minimal, backward-compatible (3–5 still true), correct (6 is engaged). States 8/9 stay
false — no safety-semantic change. Down-checked states (0/1/2/7/15) unchanged.

### Change 2 — Enable AP-First by default (D2)

Enable `INJECTION_AFTER_AP` in the waveshare build profile so `DASH_AP_GATE_DEFAULT`
becomes `true`. Concretely: add `--enable INJECTION_AFTER_AP` to the `platformio-build`
matrix profile in `.github/workflows/tests.yml` (the flag is already listed in
`OPTIONAL_DEFINES` in `scripts/platformio_set_profile.py:22`, so no script change is
needed), and uncomment `#define INJECTION_AFTER_AP` in `platformio_profile.example.h` so
local builds match. `INJECTION_AFTER_AP` is a narrow flag — it only sets
`kInjectionAfterApBuildEnabled` (constexpr) and the gate default; it adds no injection and
has its own native test env (`native_injection_after_ap`).

Effect: Legacy `0x3EE` injection is held by the existing gate until AP is stably engaged
for `ap_delay_ms` (default 2000 ms, user-adjustable via the UI `ap-delay-select`), moving
injection from the +0.2 s edge to +2 s stable — away from the `0x488` spike window. Users
may still disable the gate via the UI `ap-core-gate-tgl` to recover the old behavior — this
is the supported path for **non-8.3.6 cars** where direct Legacy `0x3EE` injection (on the
activation edge) is safe and the 2 s settle is unnecessary. The toggle is NVS-persisted
(`ap_gate`) and exposed in `/status` as `apGateEnabled`; the UI card labels it
"默认开：等 AP 稳定再注入（防 8.3.6 猛甩）。非 8.3.6 车型可关闭以直接注入".

### Safety (mandatory)

- **No new injection. No 0x370.** EPAS-nag ban preserved. Run
  `test/test_no_epas_nag_contract.py` and confirm the 8 guarded symbols remain absent.
- `isDASAutopilotActive` is a pure helper; the only callers set `APActive` in
  `handlers.h` (3 sites). None unlock EPAS (physically removed). Verify with grep.
- `INJECTION_AFTER_AP` is covered by `test_native_injection_after_ap`.

### Honest scope (must be communicated)

This fix **reduces** the jerk on our fork (data shows the gate was not effective due to
D1+D2). Upstream data shows even a working gate leaves a **<5 % residual**. Therefore:

- This does **not** guarantee zero jerk.
- UI + docs must warn: CN 2026.8.3.6 is high-risk; research/educational use only; user
  assumes all risk; prefer Listen-Only until verified on the specific car.
- **Edge-softening** (the reporter's and upstream's hypothesis for the residual) is
  explicitly **out of scope** here — it needs captures with TX echo enabled to observe the
  injected frame, and is tracked as future work (§7).

## 5. Testing (TDD)

1. `isDASAutopilotActive(6) == true` (new) and `(3),(4),(5)` still true; `(0),(1),(2),(8),(9),(15)` false.
2. Gate regression (native_legacy or native_injection_after_ap): with gate ON and AP at
   state 6, injection blocked before `ap_delay_ms` elapses, allowed after.
3. Contract: waveshare build has `INJECTION_AFTER_AP` defined → gate default true.
4. `test_no_epas_nag_contract.py` still green (8 symbols absent).

## 6. Diagnostic report (separate deliverable)

`docs/steer-jerk-diagnosis-20260623.md` — for posting to upstream #108 / ev-open-can-tools
#66 and replying to the reporter. Contains: the data table, the `0x488 = angle×10`
confirmation, the causal chain, the precise **+0.2 s** edge timing (correcting the
reporter's "~1 s" estimate), the `isDASAutopilotActive` state-6 finding (upstream may share
it), and the recommendation (AP-First is correct; edge-softening is the open frontier;
2026.8.3.6 → Listen-Only until verified; next capture should enable TX echo).

## 7. Out of scope / future work

- Edge-softening / ramp on the `0x3EE` enable bit (needs TX-echo captures).
- The latent broader bug: any logic depending on `isDASAutopilotActive` is wrong for
  state-6 cars (e.g., FSD injection gates). Fixed at the helper here, but audit other
  callers in a follow-up.
- The reporter is on **upstream** firmware; our fix is for our fork. Contributing the
  state-6 finding + diagnosis upstream is the path to helping the reporter directly.

## 8. Risks

- Defaulting the gate ON changes Legacy behavior (FSD injection now requires AP engaged +
  stable). Mitigation: UI toggle preserves the old path; documented.
- If a car engages to a state outside 3–6 with the gate ON, FSD won't activate until the
  user disables the gate (fail-closed = safe). Mitigation: documented; toggle available.
