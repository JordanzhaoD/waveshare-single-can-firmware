# Bionic Steering DND — Port to waveshare LegacyHandler (production Legacy path)

**Date:** 2026-06-25 (rev. 2 — host handler corrected: NagHandler → LegacyHandler)
**Author:** ziwind (for Jordan)
**Status:** Design — awaiting review (rev. 2)
**Origin:** "Track A" of the upstream-beta.10 alignment brainstorm, evolved into a bionic-port task.
**Related:** LILYGO T-2CAN bionic (`LILYGO-T-2Can-firmware/include/handlers.h:716-838`); local `LegacyHandler` (`include/handlers.h:295`); `dash_bionic_steer.h` (DashBionicSteer struct); `docs/EPAS-NAG-REMOVAL-INCIDENT.md`.
**Safety class:** EPAS/steering-relevant (0x370 torque injection). **Delivery scope: implement + bench-validate + flash + Jordan on-car test.**

> **Ban status.** Jordan voided the `EPAS-NAG-REMOVAL-INCIDENT.md` ban on 2026-06-25. The design proceeds on that authority; the incident's technical facts remain relevant as the *unknown* this work tests.

> **Revision 2 note (host-handler correction).** Rev. 1 targeted `NagHandler`. Planning found `NagHandler` is **test-only** — `app.h:27-44` selects `SelectedHandler` exclusively, and `#if defined(ESP32_DASHBOARD)` (defined in all production waveshare builds, `platformio.ini:26`) takes precedence over the `NAG_KILLER` branch. Production → `DASH_DEFAULT_HW` → LegacyHandler/HW3/HW4; `NagHandler` is compiled only in `native_nag`. So a bionic in NagHandler **cannot run on Jordan's car**. Jordan approved re-targeting to **`LegacyHandler`** (the production Legacy handler). This also **simplifies** the design: LegacyHandler currently has NO 0x370 echo (it listens to 880 in `filterIds` but has no `frame.id == 880` block), so there is no existing torque mode to disambiguate — the bionic is a single, opt-in echo block.

---

## 1. Background & decision chain

Track A explored aligning with upstream `flipper-tesla-fsd` v2.16-beta.10's "EPAS-faithful handsOnLevel" fix. Research across tesla-fsd-controller (handsOnLevel forge, proven safe), the public source (bionic sine = 禁招 class), and LILYGO T-2CAN (bionic wired, reportedly effective on HW3/HW4) reshaped it into: **port the LILYGO bionic to waveshare so the DND function works on Legacy**.

Jordan voided the 2026-06-19 incident ban and chose **mode (ii)**: bionic sine on the fixed `0x08B6` (1.80 Nm) tamper base (the LILYGO method). The host-handler correction (rev. 2) retargets the port from NagHandler to LegacyHandler.

**Why dual-CAN "works" is HW-version, not CAN-count:** 0x370 is on CAN-A (the EPAS bus) in both LILYGO and waveshare. "Works on HW3/HW4" = those EPAS firmwares tolerate the injection; Legacy EPAS rejected the *Mode-C* dynamic torque (2026-06-19). The bionic (fixed-base + sine) is milder-different; whether Legacy tolerates it is **untested** — the on-car test answers it.

## 2. Goal & honest scope limit

**Goal.** Add a bionic 0x370 echo block to `LegacyHandler` so the existing (orphan) `bionicSteering` toggle drives a real nag-suppression path on Jordan's Legacy car: echo `0x370` with `0x08B6` base + sine perturbation + `handsOnLevel=1` + counter+1 + checksum, when `bionicSteering` is ON + hands-off + AD-clear. Reuse the existing `DashBionicSteer` struct + `bionicSteering` flag + UI/NVS/HTTP (already wired).

**Honest scope limit.** The port is mechanical. The **real** question — *does Legacy EPAS tolerate the bionic, or fault like Mode-C?* — only on-car testing answers. Bench verifies frame format + sine + 3-fail auto-disable, NOT real EPAS tolerance. 0x370 is the EPAS's own status report; dynamic torque injection is the proven-dangerous class. Jordan accepted this risk by voiding the ban.

## 3. Architecture — bionic 0x370 echo in LegacyHandler

### 3.1 Host handler: LegacyHandler

`LegacyHandler` (`include/handlers.h:295`) is the production handler for Jordan's car (`DASH_DEFAULT_HW==0`). It already listens to `0x370` (`880` in `filterIds`, line ~302) but has **no echo block** — `handleMessage` handles 69/760/1080/921/1006 only. The port adds:

- Member `DashBionicSteer bionic;` (reuses `include/dash_bionic_steer.h`).
- Overrides `bool bionicDisabled() const override { return bionic.isDisabled(); }` and `void resetBionic(uint32_t seed) override { bionic.reset(); bionic.init(seed ? seed : 0xDEADBEEF); }` (base virtuals at `handlers.h:290-291`).

### 3.2 The 0x370 bionic echo block (single opt-in mode)

A new `if (frame.id == 880 && frame.dlc >= 8)` block in `LegacyHandler::handleMessage`, gated on `bionicSteering` (default OFF):

```cpp
if (frame.id == 880 && frame.dlc >= 8)
{
    // Bionic nag suppression (opt-in via bionicSteering; default OFF).
    uint8_t handsOn = (frame.data[4] >> 6) & 0x03;
    bool useBionic = (bool)bionicSteering && !bionic.isDisabled() && handsOn == 0;
    if (checkAD && !checkAD())
        useBionic = false;
    if (useBionic)
    {
        if (bionic.needsNewPhase())
            bionic.beginPhase();
        int pert = bionic.computePerturbation();

        CanFrame echo;
        echo.id = 880;
        echo.dlc = 8;
        echo.data[0] = frame.data[0];
        echo.data[1] = frame.data[1];
        uint8_t d2lo = 0x08;             // sign nibble positive
        uint8_t d3   = 0xB6;             // 1.80 Nm base
        bionic.applyToFrame(d2lo, d3, pert);   // sine perturbs the 0x08B6 base
        echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | d2lo);
        echo.data[3] = d3;
        echo.data[4] = static_cast<uint8_t>(frame.data[4] | 0x40);  // handsOnLevel = 1
        echo.data[5] = frame.data[5];
        uint8_t cnt = (frame.data[6] & 0x0F);
        cnt = (cnt + 1) & 0x0F;
        echo.data[6] = static_cast<uint8_t>((frame.data[6] & 0xF0) | cnt);
        uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] +
                       echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

        // Self-validation: on checksum mismatch, fall back to fixed 0x08B6 + count failure.
        uint16_t verify = sum + 0x73;
        if (echo.data[7] != static_cast<uint8_t>(verify & 0xFF))
        {
            bionic.reportFailure();
            echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | 0x08);
            echo.data[3] = 0xB6;
            uint16_t fbSum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] +
                             echo.data[4] + echo.data[5] + echo.data[6];
            echo.data[7] = static_cast<uint8_t>((fbSum + 0x73) & 0xFF);
        }
        else
        {
            bionic.reportSuccess();
        }

        framesSent++;
        driver.send(echo);
    }
    // bionicSteering OFF → no 0x370 echo (LegacyHandler's current behaviour, unchanged)
}
```

**Mode = (ii)** baked in: the echo is always the `0x08B6` tamper base + sine (when enabled). There is no separate passthrough/tamper mode for LegacyHandler — it either echoes bionic (toggle ON) or doesn't echo 0x370 at all (toggle OFF, current behaviour).

### 3.3 3-fail auto-disable

`DashBionicSteer.reportFailure()` (called on checksum mismatch) increments `consecutiveFails`; at `kMaxConsecutiveFails` (3) it sets `disabled=true`. Subsequent frames skip the bionic (`useBionic` false) → no echo until the dashboard re-arms via `resetBionic()` (the `def-bionic-tgl` OFF→ON path already calls `dashHandler->resetBionic(millis())`, `mcp2515_dashboard.h:3175`).

### 3.4 What stays unchanged

- `bionicSteering` flag, NVS `def_bio`, `/defense_config` `bionic_steering`, `/status` `bionicSteering`, UI `def-bionic-tgl`, boot-sync, `handlerPool[i]->bionicSteering = ...` — **all already wired**; they now drive the LegacyHandler bionic path.
- LegacyHandler's existing handling of 69/760/1080/921/1006/0x3EE — **unchanged**.
- `handsOnLevel=1` forge (within the bionic echo) — retained (tesla-fsd-controller/LILYGO forge it; upstream beta.10 "stop forging" is out of scope).
- `NagHandler` (test-only, `native_nag`) — **untouched**; its Phase-1 dual-mode stays as-is.

## 4. File changes

| File | Action | Notes |
|---|---|---|
| `include/handlers.h` | **modify** `LegacyHandler` (`:295`): add `DashBionicSteer bionic;` member, `bionicDisabled()`/`resetBionic()` overrides, the `if (frame.id == 880)` bionic echo block in `handleMessage`. | core port |
| `include/dash_bionic_steer.h` | **none** | reuse as-is |
| `include/web/mcp2515_dashboard.h` | **none** | `bionicSteering`/`def_bio`/boot-sync all already wired |
| `include/web/mcp2515_dashboard_ui.src.h` | **modify (minor)** | add 高危/严禁上车 warning chip (`def-bionic-risk`) mirroring `def-ntt-warn`; regen `ui.h` |
| `test/test_native_legacy/test_legacy_handler.cpp` | **add** | bionic-path tests (sine on base, cap, 3-fail disable, gate, OFF=no-echo) | TDD |
| `test/test_no_epas_nag_contract.py` | **add assertion** | bionicSteering default-off + gated in LegacyHandler; 8 banned symbols stay absent |
| `platformio.ini` / waveshare profile | **none** | LegacyHandler is always compiled in production; bionic gated by `bionicSteering` (no new build flag) |

## 5. Defaults & configurability

| Setting | Default | Tunable | Rationale |
|---|---|---|---|
| Bionic enabled (`bionicSteering`) | **false** (OFF) | UI `def-bionic-tgl`, NVS `def_bio` | opt-in; on-car enable by Jordan |
| Bionic amplitude | 30–55 raw | compile-time (`dash_bionic_steer.h`) | LILYGO-proven range |
| Bionic perturbation cap | 60 raw | compile-time | safety cap |
| Bionic phase duration | 350–500 ms | compile-time | LILYGO-proven |
| 3-fail auto-disable | 3 anomalies | `kMaxConsecutiveFails` | auto-fallback |
| Gate | `bionicSteering && !disabled && handsOn==0 && checkAD` | — | opt-in + nag-state + AD-clear |

**Note:** unlike the NagHandler path, the LegacyHandler bionic runs in **production** (LegacyHandler is always compiled). It is gated solely by `bionicSteering` (default OFF) — no `NAG_KILLER` build flag involved.

## 6. Safety (ban voided; guardrails retained)

Jordan voided the incident ban on 2026-06-25. Guardrails retained as sound engineering:

1. **Opt-in, default OFF.** `bionicSteering=false` at boot.
2. **3-fail auto-disable.** `DashBionicSteer` disables after 3 checksum anomalies (no echo until re-arm).
3. **Bounded perturbation.** Sine capped at ±60 raw around the 1.80 Nm base.
4. **Gate.** Fires only when hands-off (`handsOn==0`) + AD-clear — i.e. only in the nag state.
5. **No new EPAS symbols.** The 8 banned `DashEpasNag` symbols stay absent (`test_no_epas_nag_contract.py` green — `DashBionicSteer` is not one of them).

**Honest risk:** this is 0x370 dynamic torque injection on Legacy — same message + hazard class as the 2026-06-19 incident. The bionic (fixed-base + sine) is milder-different than Mode-C; **only on-car confirms** tolerance. First enable in a safe area, hands ready; on any EPAS fault/warning → disable immediately.

## 7. Testing

- **Unit (new, TDD, `native_legacy` env, `test_legacy_handler.cpp`):** (a) `bionicSteering=false` → no 0x370 echo (`mock.sent` empty for 880); (b) `bionicSteering=true` + handsOn==0 → echo sent, first frame torque == `0x08B6` (sin(0)=0); (c) bionic ON over many frames → torque varies (sine active); (d) all echo torques within `[0x08B6-60, 0x08B6+60]` (cap); (e) handsOn!=0 → no echo (gate); (f) `bionic.isDisabled()` (3 fails) → no echo; (g) checksum always valid.
- **Contract:** `test_no_epas_nag_contract.py` — add `bionicSteering` default-off + LegacyHandler-gated assertion; 8 `DashEpasNag` symbols stay absent.
- **Regression:** existing `native_legacy` tests stay green (bionic OFF by default → LegacyHandler behaviour unchanged).
- **Build:** `pio run -e waveshare_single_can_standalone` SUCCESS (LegacyHandler compiled; bionic compiled but inert unless toggled).
- **Bench cannot validate EPAS tolerance.**
- **On-car (Jordan):** flash → enable `def-bionic-tgl` in a safe area → (1) nag suppress? (2) any EPAS fault? If fault → disable + report.

## 8. Open risks

- **Legacy EPAS tolerance is THE unknown** — only on-car tells. Jordan accepted this by voiding the ban.
- **0x370 injection inherent risk** — counter-collision + ID-competition with real EPAS (incident doc 35b).
- **Production exposure** — LegacyHandler bionic runs in production when toggled (unlike NagHandler which was test-only). The default-OFF + 3-fail-disable + gate are the guardrails.
- **Counter strategy** — `+1` interleave was bench-validated collision-free for the fixed echo (Phase-1 F1); re-confirm with the bionic active.

## 9. Out of scope / deferred

- Upstream beta.10 "stop forging handsOnLevel" — not pursued (forge retained per tesla-fsd-controller/LILYGO).
- 0x399/0x39B nag-level TX forge — separate future exploration.
- The public-source reactive 0x399-nag≥3 torque BURST — NOT ported (most aggressive technique); only the continuous bionic sine is ported.
- Re-tuning bionic amplitude/duration for Legacy — ship LILYGO defaults, tune on-car.
- NagHandler bionic (test-only) — left as Phase-1 state (PASSTHROUGH/tamper); not extended.
