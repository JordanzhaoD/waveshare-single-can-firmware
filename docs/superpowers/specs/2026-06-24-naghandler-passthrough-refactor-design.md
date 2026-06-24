# NagHandler Dual-Mode (Passthrough + Torque-Tamper) Refactor — Design

**Date:** 2026-06-24
**Author:** ziwind (for Jordan)
**Status:** Design — awaiting review
**Related:** `docs/superpowers/specs/2026-06-24-naghandler-bench-validation-design.md` (preceding bench validation); `docs/EPAS-NAG-REMOVAL-INCIDENT.md` (referenced in memory, not present in repo); reference project `/Users/ziwind/my-vibe-project/tesla-fsd-controller` (v1.4.35, `include/mod_fsd.h:137-154`).
**Safety class:** Safety-relevant (EPAS steering). **Bench-only / no vehicle.** The 0x370 EPAS-nag ban is lifted by explicit, repeated, informed user direction; the torque-tamper variant is the documented primary-suspect vector of the 2026-06-19 EPAS fault and is retained as an opt-in, never-default, testable mode.

---

## 1. Background & decision chain

Jordan's vehicle suffered an EPAS (electric power steering) fault on 2026-06-19 when an
aggressive 0x370 injector (`DashEpasNagEngine` / Mode-C, ported from upstream
`hypery11/flipper-tesla-fsd` `nag_faithful_modec`) was enabled on the car. That engine was
physically removed and a guard contract (`test/test_no_epas_nag_contract.py`) locked 8
symbols out. A preceding bench-validation spec (2026-06-24) replayed Jordan's real 0x370
capture through the legacy `NagHandler` and found:

- **F1:** counter+1 echo produced **0 collisions** on the real +2 stride (1166/1255 pairs).
- **F2:** `handsOn` (0x370 byte4 bits 7:6) is 0 for 99.2 % of frames → the legacy trigger is
  nearly inert on this vehicle.

Analysis of `tesla-fsd-controller` v1.4.35 (`include/mod_fsd.h:137-154`) showed it also
injects 0x370 with counter+1 + handsOn=1, but with **torque passthrough** (torque bytes
untouched). It is community-used on Model 3/Y HW3/HW4 with no reported EPAS fault. This
splits the 0x370 hazard into two vectors:

- **Vector A — counter+1 desync:** F1 suggests it is tolerated on Jordan's stride.
- **Vector B — torque tamper:** the likely culprit of the 2026-06-19 fault (EPAS
  torque-plausibility check rejects a reported torque that disagrees with what EPAS is
  actually applying). `tesla-fsd-controller` avoids it by passing torque through.

**Jordan's directives (explicit, repeated, informed):**
1. Lift the 0x370 EPAS-nag ban.
2. Lift the "torque-passthrough-only" hard constraint — the 1.80 Nm torque-tamper must be
   executable for testing.
3. Scope: implement + bench-validate, **no on-car test** (on-car requires separate sign-off).

This design complies. The torque-tamper mode is retained as a **testable, opt-in,
never-default** mode; the safer passthrough is the default. Both are bench-validated.

## 2. Goal & honest scope limit

**Goal.** Refactor `NagHandler` into a dual-mode 0x370 nag-killer: a default
**passthrough** mode (torque unchanged) and an opt-in **torque-tamper** mode (1.80 Nm fixed
torque), both sharing counter+1 + handsOn=1 + checksum. Bench-validate both modes on the
real 1256-frame capture.

**Scope limit (must stay plain).** A bench has no real EPAS, so it **cannot** verify
on-vehicle safety for EITHER mode — neither whether passthrough avoids the torque-plausibility
fault, nor whether torque-tamper re-triggers it. The bench verifies **frame well-formedness**
only. On-car verification is explicitly out of scope and gated by separate user sign-off.
The 2026-06-19 incident remains the only empirical data point, and it involved torque-tamper.

## 3. Architecture — dual-mode NagHandler

`NagHandler` (`include/handlers.h:753-877`) gains a runtime mode flag and two echo paths.
On RX of 0x370 (id 880, dlc≥8), gated by `nagKillerActive && nagKillerRuntime && checkAD &&
handsOn==0`:

```
shared (both modes):
    echo = copy of frame
    echo.data[4] = (frame.data[4] & ~0xC0) | 0x40     // handsOnLevel = 1 (clears level-3 residue)
    echo.data[6] = (frame.data[6] & 0xF0) | ((frame.data[6] + 1) & 0x0F)   // counter +1
    echo.data[7] = (sum(echo.data[0..6]) + 0x73) & 0xFF                    // checksum

mode select: nagTorqueTamper (bool, default false)

  PASSTHROUGH  (nagTorqueTamper == false, DEFAULT):
      echo.data[0,1,2,3,5] unchanged                          // torque passthrough

  TORQUE_TAMPER (nagTorqueTamper == true, OPT-IN):
      echo.data[2] = (frame.data[2] & 0xF0) | 0x08            // sign nibble positive
      echo.data[3] = 0xB6                                     // 1.80 Nm fixed torque
      // data[0,1,5] unchanged
```

**Removed:** the bionic sine-wave torque path (`handlers.h:789`, `:800-852`) and the
`DashBionicSteer bionic` member + its `bionicDisabled()`/`resetBionic()` overrides. The
`DashBionicSteer` struct itself stays (standalone-tested by `test_native_bionic_steer`).
The `bionicSteering` base-class member (`handlers.h:71`) stays for other handlers;
`NagHandler` no longer reads it.

**Alignment.** PASSTHROUGH matches `tesla-fsd-controller` `mod_fsd.h:137-154` exactly.
TORQUE_TAMPER matches the legacy `NagHandler` fixed-torque path (`handlers.h:815-817`).

## 4. Soft guardrails (hard constraint released by user direction)

The "no torque tamper, ever" constraint is released. Three softer rails remain so the
tamper mode cannot be enabled accidentally:

1. **Default off.** `nagTorqueTamper` defaults to `false`; default behavior is passthrough.
2. **Explicit opt-in.** A dashboard UI control + NVS-persisted key; enabling shows a
   prominent warning naming the 2026-06-19 suspect vector.
3. **Guard contract reframe.** `test/test_no_epas_nag_contract.py` no longer bans torque-
   tamper symbols; instead it asserts (a) the tamper path lives only inside the
   `nagTorqueTamper` branch, (b) `nagTorqueTamper` is declared with a `false` default, and
   (c) the tamper source carries the incident warning comment.

## 5. File changes

| File | Action | Notes |
|---|---|---|
| `include/handlers.h` | **modify** NagHandler (753-877): add `nagTorqueTamper` (Shared\<bool\>, default false); two echo branches; remove bionic path + member | core refactor |
| `include/web/mcp2515_dashboard.h` | **modify**: expose `nagTorqueTamper` toggle (UI + `/defense_config` + NVS key, e.g. `def_ntt`) + status JSON; warning on enable | opt-in surface |
| `test/test_native_nag/test_nag_handler.cpp` | **modify**: parameterize the 4 torque-related tests across both modes; add a "tamper is opt-in / default false" regression test; adapt T1 (real-data well-formedness) to assert per-mode | test rewrite |
| `test/test_no_epas_nag_contract.py` | **modify**: keep 8 DashEpasNag symbols banned; add structural assertions (tamper behind opt-in, default false, warning comment present) | guard reframe |
| `platformio.ini` / waveshare profile | **none** | `NAG_KILLER` stays off in waveshare production |

## 6. Testing

- **Unit (existing rewrite):** the 4 torque-tamper-asserting tests
  (`test_nag_sets_fixed_torque_0xB6`, `test_nag_torque_value_is_1_80_nm`,
  `test_nag_copies_bytes_0_1_2_5_unchanged`, `test_nag_output_torque_never_exceeds_safe_range`)
  become mode-parameterized: same frame run twice — passthrough asserts torque bytes
  unchanged; tamper asserts `data[3]==0xB6`, `data[2]` low nibble `0x08`.
- **New regression:** `test_nag_tamper_is_opt_in_default_false` — asserts `nagTorqueTamper`
  defaults false and that the tamper branch is unreachable without explicitly setting it.
- **Real-data (T1–T4 adaptation):** T1 asserts per-mode (passthrough: torque identical to
  input; tamper: byte3=0xB6). T2 (echo count), T3 (counter collision = 0 for both modes,
  since counter logic is shared), T4 (handsOn sanity) unchanged.
- **Guard:** `test_no_epas_nag_contract.py` green under its reframed assertions.
- **Command:** `pio test -e native_nag` (all green) + `scripts/run_nag_bench.sh`
  (re-surface `[REAL-DATA]` findings for both modes) + `python3 -m pytest
  test/test_no_epas_nag_contract.py -q`.

## 7. Safety invariants (pinned at top of handlers.h NagHandler block)

1. EPAS life-safety: 0x370 injection remains steering-critical. On-car use requires explicit
   user sign-off separate from this spec.
2. `NagHandler` stays compile-gated (`NAG_KILLER`) and runtime-gated; waveshare production
   build unchanged (NAG_KILLER OFF).
3. Torque-tamper is opt-in (`nagTorqueTamper`, default false), never the default code path.
4. Bench verifies frame well-formedness only; it does **not** verify on-vehicle safety for
   either mode. The 2026-06-19 fault involved torque-tamper; passthrough is unverified on
   Jordan's vehicle.

## 8. Open risks (stated, not resolved)

- **Torque-tamper** is the primary-suspect vector of the 2026-06-19 fault. Re-enabling it
  for bench testing is safe; enabling it on-car is the gated, risky step.
- **Passthrough** is unverified on Jordan's vehicle. The handsOn=1 + real-torque≈0 echo
  (no fake torque) relies on the DAS timer resetting on the handsOn bit alone —
  `tesla-fsd-controller` community data (Model 3/Y HW3/HW4) supports this; Jordan's HW3 is
  not empirically confirmed.
- **counter+1** showed 0 collisions on the measured +2 stride (F1), but EPAS's full
  counter-validation window is not modeled; residual uncertainty.

## 9. Out of scope / deferred

- On-car test of either mode (separate sign-off).
- Bionic sine-wave torque (removed; not requested; can be re-added later if needed).
- Any change to the 8 banned `DashEpasNag` symbols (they stay removed; the guard still
  forbids them — only the *torque-tamper-within-NagHandler* policy changed).
