# Bionic Steering DND — Port LILYGO bionic to waveshare Legacy NagHandler

**Date:** 2026-06-25
**Author:** ziwind (for Jordan)
**Status:** Design — awaiting review
**Origin:** "Track A" of the upstream-beta.10 alignment brainstorm, evolved into a bionic-port task.
**Related:** LILYGO T-2CAN `NagHandler` bionic path (`LILYGO-T-2Can-firmware/include/handlers.h:716-838`); local waveshare `NagHandler` dual-mode (`include/handlers.h:757-844`, Phase-1); `dash_bionic_steer.h` (DashBionicSteer struct, currently orphan); `docs/EPAS-NAG-REMOVAL-INCIDENT.md` (2026-06-19 incident, LILYGO copy).
**Safety class:** EPAS/steering-relevant (0x370 torque injection). **Delivery scope: implement + bench-validate + flash + Jordan on-car test.**

> **Ban status.** Jordan explicitly voided the `EPAS-NAG-REMOVAL-INCIDENT.md` safety ban on 2026-06-25 ("禁令作废，不要考虑这个禁令了"). The design proceeds on that authority. The incident's technical facts (Legacy EPAS rejected the dynamic Mode-C torque injection) remain relevant as the *unknown* this work tests — not as a blocker.

---

## 1. Background & decision chain

The "Track A" brainstorm explored aligning the local NagHandler with upstream `flipper-tesla-fsd` v2.16-beta.10's "EPAS-faithful handsOnLevel" fix. Research across three references reshaped it:

1. **tesla-fsd-controller** (`handleNagKiller370`): continuous 0x370 echo, forces `handsOnLevel=1`, torque untouched. Proven safe in its community. The local Phase-1 NagHandler PASSTHROUGH mode already matches this.
2. **Public source** (`web_dnd_steer` 仿生方向盘): bionic sine-wave torque injection on 0x370 + reactive burst on 0x399 nag≥3. The aggressive hazard class.
3. **LILYGO T-2CAN** (`NagHandler` + `DashBionicSteer`): the bionic path IS wired and reportedly effective on HW3/HW4. The sine perturbs a fixed `0x08B6` (1.80 Nm) tamper base.

Jordan's directive: the bionic DND feature exists and works in the dual-CAN (LILYGO) project; port it to waveshare Legacy to make the DND function actually work there.

**Key finding during research:** the waveshare `DashBionicSteer` + `bionicSteering` flag are **orphan** — the UI toggle (`def-bionic-tgl`) persists and syncs the flag, but no handler reads `bionicSteering` or calls `DashBionicSteer` methods (Phase-1 removed the NagHandler bionic path). So waveshare's "bionic ineffective on Legacy" is **dead code, not EPAS rejection** — it has never actually run on Legacy. Whether Legacy EPAS *tolerates* the bionic (vs the Mode-C it rejected in 2026-06-19) is **untested** and is the question this work answers.

**Why dual-CAN "works" is HW-version, not CAN-count:** 0x370 is on CAN-A (the EPAS bus) in both LILYGO and waveshare. The bionic injects there identically. "Works on HW3/HW4" = HW3/HW4 EPAS tolerates the injection; Legacy EPAS rejected Mode-C. Porting tests whether Legacy tolerates the (milder, fixed-base+sine) bionic.

**Mode interaction chosen: (ii)** — bionic sine on the fixed `0x08B6` tamper base (the LILYGO method).

## 2. Goal & honest scope limit

**Goal.** Port the LILYGO `NagHandler` bionic path into the waveshare `NagHandler`, wiring the existing (orphan) `DashBionicSteer` struct + `bionicSteering` flag so the toggle actually drives a bionic torque path: 0x370 echo with `0x08B6` base + sine perturbation + `handsOnLevel=1` + counter+1 + checksum. Three-mode precedence: **bionic > fixed-tamper > passthrough**.

**Honest scope limit (stated plainly).** The port is mechanical (the code exists in LILYGO; the pieces exist in waveshare). The **real** question — *does Legacy EPAS tolerate the bionic, or does it fault like Mode-C did?* — can only be answered by **on-car testing**, which is Jordan's. The bench verifies the frame format + sine logic + 3-fail auto-disable, NOT real EPAS tolerance. There is inherent risk: 0x370 is the EPAS's own status report, and dynamic torque injection is the proven-dangerous class (2026-06-19). Jordan has accepted this risk by voiding the ban.

## 3. Architecture — three-mode NagHandler with bionic precedence

### 3.1 The port (LILYGO → waveshare)

Add to waveshare `NagHandler` (`include/handlers.h:757`), mirroring LILYGO `handlers.h:716-838`:

- Member `DashBionicSteer bionic;` (reuses the existing `include/dash_bionic_steer.h`).
- Overrides `bool bionicDisabled() const override { return bionic.isDisabled(); }` and `void resetBionic(uint32_t seed) override { bionic.reset(); bionic.init(seed ? seed : 0xDEADBEEF); }` (the base virtuals at `handlers.h:290-291` already exist).

### 3.2 Three-mode torque logic (bionic > tamper > passthrough)

In `handleMessage`, after the existing gate (`handsOn==0` + `checkAD` + `nagKillerActive`/`nagKillerRuntime`), replace the Phase-1 two-branch torque block with three branches:

```cpp
bool useBionic = (bool)bionicSteering && !bionic.isDisabled();

echo.data[0] = frame.data[0];
echo.data[1] = frame.data[1];
echo.data[5] = frame.data[5];

if (useBionic)
{
    // ── Bionic: 0x08B6 tamper base + sine perturbation (LILYGO method) ──
    if (bionic.needsNewPhase()) bionic.beginPhase();
    int pert = bionic.computePerturbation();
    uint8_t d2lo = 0x08;
    uint8_t d3   = 0xB6;
    bionic.applyToFrame(d2lo, d3, pert);                 // perturbs the 0x08B6 base
    echo.data[2] = (frame.data[2] & 0xF0) | d2lo;
    echo.data[3] = d3;
}
else if (nagTorqueTamperRuntime)
{
    // ── Fixed tamper: 1.80 Nm (0x08B6), no sine (Phase-1 opt-in) ────────
    echo.data[2] = (frame.data[2] & 0xF0) | 0x08;
    echo.data[3] = 0xB6;
}
else
{
    // ── Passthrough: torque bytes untouched (Phase-1 default) ──────────
    echo.data[2] = frame.data[2];
    echo.data[3] = frame.data[3];
}

// handsOnLevel = 1 (all modes — unchanged)
echo.data[4] = frame.data[4] | 0x40;

// Counter + 1, checksum (unchanged)
// ... (existing counter + checksum code)
```

### 3.3 Bionic self-validation (3-fail auto-disable)

After computing the checksum, when `useBionic`, re-verify it (LILYGO `handlers.h:794-815`):

```cpp
if (useBionic)
{
    uint16_t verify = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3]
                    + echo.data[4] + echo.data[5] + echo.data[6] + 0x73;
    uint8_t expectedCs = static_cast<uint8_t>(verify & 0xFF);
    if (echo.data[7] != expectedCs)
    {
        bionic.reportFailure();                          // anomaly → counts toward disable
        // Fall back to fixed 0x08B6 echo + recompute checksum
        echo.data[2] = (frame.data[2] & 0xF0) | 0x08;
        echo.data[3] = 0xB6;
        uint16_t fbSum = echo.data[0] + echo.data[1] + echo.data[2] +
                         echo.data[3] + echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((fbSum + 0x73) & 0xFF);
    }
    else
    {
        bionic.reportSuccess();
    }
}
```

`DashBionicSteer` auto-disables after `kMaxConsecutiveFails` (3) anomalies (`reportFailure()` increments `consecutiveFails`; at 3 sets `disabled=true`). The dashboard re-arm (`def-bionic-tgl` OFF→ON → `resetBionic`) clears it. This is the existing `DashBionicSteer` behaviour — no new logic.

### 3.4 What stays unchanged

- `bionicSteering` flag, NVS `def_bio`, `/defense_config` `bionic_steering`, `/status` `bionicSteering`, UI `def-bionic-tgl`, boot-sync (`dashHandler->bionicSteering = dashBionicSteering`), `handlerPool[i]->bionicSteering = ...` — **all already wired** (Phase-1 + earlier). They now actually drive the bionic path.
- `nagTorqueTamperRuntime` (Phase-1 fixed-tamper opt-in) — unchanged, second-precedence mode.
- Gate (`handsOn==0` + `checkAD` + `nagKillerActive`/`nagKillerRuntime`) — unchanged.
- `handsOnLevel=1` forge (all modes) — unchanged (the upstream beta.10 "stop forging" is out of scope; Jordan chose to follow tesla-fsd-controller/LILYGO which forge it).
- `NAG_KILLER` build flag (production waveshare OFF → NagHandler not registered → entire path inert in release).

## 4. File changes

| File | Action | Notes |
|---|---|---|
| `include/handlers.h` | **modify** `NagHandler` (`:757`): add `DashBionicSteer bionic;` member, `bionicDisabled()`/`resetBionic()` overrides, three-mode torque logic, bionic self-validation. | core port |
| `include/dash_bionic_steer.h` | **none** | reuse as-is (struct already exists, correct algorithm) |
| `include/web/mcp2515_dashboard.h` | **none** | `bionicSteering`/`def_bio`/`def-bionic-tgl`/boot-sync all already wired |
| `include/web/mcp2515_dashboard_ui.src.h` | **modify (minor)** | `def-bionic-tgl` UI already present; add a 「高危/严禁上车」warning chip + experiment-summary chip mirroring `def-ntt-tgl` (the bionic is incident-class; the toggle should warn like the torque-tamper one). Regen `ui.h`. |
| `test/test_native_nag/test_nag_handler.cpp` | **add + modify** | add bionic-path tests (sine on base, 3-fail disable, fallback, precedence); update any test that assumed the removed bionic path | TDD |
| `test/test_no_epas_nag_contract.py` | **add assertion** | assert `DashBionicSteer` usage is gated by `bionicSteering` (default-off), parallel to the `nagTorqueTamper` guard | guard |
| `platformio.ini` / waveshare profile | **none** | rides existing `NAG_KILLER` (native envs) + production-off |

## 5. Defaults & configurability

| Setting | Default | Tunable | Rationale |
|---|---|---|---|
| Bionic enabled (`bionicSteering`) | **false** (OFF) | UI `def-bionic-tgl`, NVS `def_bio` | opt-in; the existing default. On-car enable by Jordan |
| Bionic amplitude | 30–55 raw (`DashBionicSteer`) | compile-time (`dash_bionic_steer.h`) | LILYGO-proven range |
| Bionic perturbation cap | 60 raw | compile-time | safety cap (existing) |
| Bionic phase duration | 350–500 ms | compile-time | LILYGO-proven |
| 3-fail auto-disable | 3 consecutive anomalies | compile-time (`kMaxConsecutiveFails`) | auto-fallback to fixed echo |
| Relation to tamper | bionic **precedence** (bionic→tamper→passthrough) | — | bionic uses the 0x08B6 base; tamper is the no-sine fallback |
| `NAG_KILLER` (production) | OFF | build flag | NagHandler not registered in release → entire path inert (defense-in-depth) |

## 6. Safety (ban voided; guardrails retained)

Jordan voided the `EPAS-NAG-REMOVAL-INCIDENT.md` ban on 2026-06-25. The design proceeds. The following guardrails are retained as sound engineering (not ban compliance):

1. **Opt-in, default OFF.** `bionicSteering=false` at boot; UI toggle to enable.
2. **`NAG_KILLER`-gated.** Production waveshare build has `NAG_KILLER` OFF → NagHandler unregistered → bionic code is runtime-inert in the release firmware even if the toggle is on.
3. **3-fail auto-disable.** `DashBionicSteer` disables after 3 checksum anomalies, falling back to fixed echo.
4. **Bounded perturbation.** Sine capped at ±60 raw around the 1.80 Nm base.
5. **No new EPAS symbols.** The 8 banned `DashEpasNag` symbols stay absent (the guard test `test_no_epas_nag_contract.py` remains green — `DashBionicSteer` is not one of them).

**Honest risk (stated, not resolved):** this is 0x370 dynamic torque injection on Legacy — the same message + hazard class as the 2026-06-19 incident. The bionic (fixed-base + sine) is a *milder, different* dynamic than Mode-C (0→2.1 Nm ramp + derived handsOn), so it *may* be tolerated where Mode-C wasn't — but **only on-car testing confirms**. First enable must be in a safe area with hands ready; if the EPAS faults (warning, body-control disable), disable the toggle immediately.

## 7. Testing

- **Unit (new, TDD, `native_nag` env):** with NAG_KILLER defined — (a) `bionicSteering=false` → passthrough (torque untouched); (b) `bionicSteering=true` + `nagTorqueTamperRuntime=true` → **bionic path wins** (base 0x08B6 + non-zero sine perturbation when a phase is active); (c) bionic sine stays within ±60 raw cap; (d) `bionicSteering=true` but `bionicDisabled` (after 3 fails) → falls back to fixed tamper; (e) checksum-anomaly injection → `reportFailure` + fixed-echo fallback; (f) precedence: bionic OFF + tamper ON → fixed 0x08B6; (g) `resetBionic()` clears the disabled state.
- **Contract:** `test_no_epas_nag_contract.py` — add `bionicSteering` default-off + gated-usage assertion; confirm the 8 `DashEpasNag` symbols stay absent.
- **Regression:** existing `native_nag` 34/34 stays green when `bionicSteering=false` (default-off → existing passthrough/tamper behaviour unchanged).
- **Build:** `pio run -e waveshare_single_can_standalone` SUCCESS (production NAG_KILLER OFF → bionic compiled but inert).
- **Bench cannot validate EPAS tolerance** — only frame format + sine + auto-disable.
- **On-car (Jordan):** flash → enable `def-bionic-tgl` in a safe area → observe (1) does the nag suppress? (2) any EPAS fault / body-control warning? If fault → disable immediately + report. This is the real validation of Legacy tolerance.

## 8. Open risks (stated, not resolved)

- **Legacy EPAS tolerance is THE unknown.** Mode-C faulted; bionic is milder-different; only on-car tells. This is the core risk Jordan accepted by voiding the ban.
- **0x370 injection inherent risk.** Counter-collision + ID-competition with the real EPAS exist for any 0x370 injection (incident doc line 35(b)). The bionic doesn't eliminate these.
- **Counter strategy.** LILYGO bionic uses counter+1 (same as the retained NagHandler). On Legacy's real stride, the +1 interleave was bench-validated collision-free (Phase-1 bench-validation F1) — but that was for the fixed echo, not the bionic. Re-confirm no collision with the bionic active.
- **handsOnLevel forge retained.** Upstream beta.10 says stop forging it; this design keeps it (following tesla-fsd-controller/LILYGO). On Jordan's car, byte4 is 99.2% static (bench F2), so the forge is moot there regardless.

## 9. Out of scope / deferred

- Upstream beta.10 "stop forging handsOnLevel" (Track A original) — not pursued; the forge is retained per tesla-fsd-controller/LILYGO precedent.
- 0x399/0x39B nag-level TX forge — separate future exploration (DAS_status forge, different hazard).
- The reactive 0x399-nag≥3 torque BURST (public-source vector 3) — NOT ported (the most aggressive public-source technique); only the continuous bionic sine (vectors 1) is ported.
- Re-tuning the bionic amplitude/duration for Legacy — ship LILYGO defaults, tune on-car.
