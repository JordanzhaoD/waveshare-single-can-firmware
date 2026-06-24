# Bionic Steering DND — Port to waveshare LegacyHandler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the LILYGO bionic steering DND into the waveshare **`LegacyHandler`** (the production Legacy handler) — add a `0x370` bionic echo block (sine perturbation on the `0x08B6` tamper base) gated by the existing (orphan) `bionicSteering` toggle, so the DND function actually runs on Jordan's Legacy car.

**Architecture:** `LegacyHandler` (`include/handlers.h:295`) is the production handler for Jordan's car (`DASH_DEFAULT_HW==0`; `NagHandler` is test-only — `app.h:27-44`, `ESP32_DASHBOARD` precedence). LegacyHandler already listens to `0x370` (`880` in `filterIds`) but has no echo block. This port adds: a `DashBionicSteer bionic;` member + `bionicDisabled()`/`resetBionic()` overrides + a single opt-in `if (frame.id == 880)` bionic echo block (mode ii: `0x08B6` base + sine + `handsOnLevel=1` + counter+1 + checksum + self-validation). The `bionicSteering` flag + UI/NVS/HTTP are already wired; this makes them drive the path.

**Tech Stack:** ESP-IDF + PlatformIO (ESP32-S3); TWAI CAN; Unity native tests (`native_legacy` env → `LegacyHandler` directly); Python `pytest` contract tests; minified dashboard UI.

**Spec:** `docs/superpowers/specs/2026-06-25-bionic-steer-port-legacy-design.md` (rev. 2 — LegacyHandler host).

**Safety context (read before starting):**
- Jordan **voided** the `EPAS-NAG-REMOVAL-INCIDENT.md` ban on 2026-06-25 — work proceeds on that authority.
- This is **0x370 dynamic torque injection on Legacy** — same hazard class as the 2026-06-19 incident. The bionic (fixed-base + sine) is milder-different than Mode-C, but **Legacy EPAS tolerance is untested** — only on-car validation (Jordan, Task 5) answers it.
- **Production exposure:** unlike the test-only NagHandler, LegacyHandler IS compiled in production → the bionic runs in production when `bionicSteering` is toggled ON. Guardrails: default OFF, gate (handsOn==0 + checkAD), 3-fail auto-disable, ±60 raw cap.
- Git: `git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local"` per commit. Local `main`; do NOT push unless told.
- Bash cwd resets every call — prefix commands with `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && ...`.
- IDE `clang` errors on `unity.h`/`Arduino.h` are false positives — `pio test`/`pio run` are truth.

---

### Task 1: LegacyHandler bionic port (TDD core)

**Files:**
- Modify: `include/handlers.h` — `LegacyHandler` struct (`:295`): add member + overrides; add `if (frame.id == 880)` bionic echo block in `handleMessage` (`:307`).
- Test: `test/test_native_legacy/test_legacy_handler.cpp` — add `makeEpasFrame` helper + `bionicSteering` reset in `setUp` + 7 bionic tests.

- [ ] **Step 1: Add `bionicSteering` reset to `setUp()` + the `makeEpasFrame` helper**

In `test/test_native_legacy/test_legacy_handler.cpp`, update `setUp()` (line ~22) to also reset the bionic flag — add this line inside `setUp()` after the existing resets:

```cpp
    handler.bionicSteering = false; // default = bionic OFF; bionic tests opt in explicitly
```

Then add a frame helper near the top (after the `denyByApGate()` function, before `setUp()`), mirroring the NagHandler test's helper:

```cpp
// Helper: build a realistic CAN 880 (0x370 EPAS3P_sysStatus) frame.
static CanFrame makeEpasFrame(uint8_t handsOn, float torqueNm, uint8_t counter)
{
    CanFrame f = {.id = 880, .dlc = 8};
    f.data[0] = 0x12;
    f.data[1] = 0x00;
    uint16_t tRaw = static_cast<uint16_t>((torqueNm + 20.5) / 0.01);
    f.data[2] = static_cast<uint8_t>(0x08 | ((tRaw >> 8) & 0x0F));
    f.data[3] = static_cast<uint8_t>(tRaw & 0xFF);
    f.data[4] = static_cast<uint8_t>(((handsOn & 0x03) << 6) | 0x1F);
    f.data[5] = 0x89;
    f.data[6] = static_cast<uint8_t>((2 << 5) | (counter & 0x0F));
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return f;
}

static int32_t decodeEchoTorqueRaw(const CanFrame &f)
{
    return static_cast<int32_t>(((f.data[2] & 0x0F) << 8) | f.data[3]);
}
```

- [ ] **Step 2: Write the 7 failing bionic tests**

Append these tests (before `int main()`):

```cpp
// ============================================================
// Bionic steering (sine on 0x08B6 base) — opt-in via bionicSteering, in LegacyHandler
// ============================================================

// Bionic OFF (default) → no 0x370 echo at all.
void test_legacy_bionic_off_no_echo()
{
    handler.bionicSteering = false;
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// Bionic ON + handsOn==0 → echo sent; first frame torque == base 0x08B6 (sin(0)=0).
void test_legacy_bionic_first_frame_is_base_torque()
{
    handler.bionicSteering = true;
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_INT32(0x08B6, decodeEchoTorqueRaw(mock.sent[0]));
}

// Bionic ON over many frames → sine produces at least one non-base torque.
void test_legacy_bionic_sine_varies_over_frames()
{
    handler.bionicSteering = true;
    bool varied = false;
    for (uint8_t c = 0; c < 40; ++c)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, 0.10, c);
        handler.handleMessage(f, mock);
        if (mock.sent.size() == 1 && decodeEchoTorqueRaw(mock.sent[0]) != 0x08B6)
            varied = true;
    }
    TEST_ASSERT_TRUE(varied);
}

// Bionic ON → echo torque always within [base - cap, base + cap] = [2170, 2290].
void test_legacy_bionic_respects_perturbation_cap()
{
    handler.bionicSteering = true;
    for (uint8_t c = 0; c < 60; ++c)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, 0.10, c);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());
        int32_t t = decodeEchoTorqueRaw(mock.sent[0]);
        TEST_ASSERT_TRUE(t >= 2230 - 60);
        TEST_ASSERT_TRUE(t <= 2230 + 60);
    }
}

// Bionic ON + handsOn!=0 → no echo (gate: only when hands off).
void test_legacy_bionic_hands_on_no_echo()
{
    handler.bionicSteering = true;
    CanFrame f = makeEpasFrame(1, 0.10, 0x0C); // handsOn=1
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// Bionic ON but auto-disabled (3 fails) → no echo until re-arm.
void test_legacy_bionic_disabled_no_echo()
{
    handler.bionicSteering = true;
    handler.bionic.reportFailure();
    handler.bionic.reportFailure();
    handler.bionic.reportFailure();
    TEST_ASSERT_TRUE(handler.bionic.isDisabled());
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// Bionic echo checksum is always valid.
void test_legacy_bionic_echo_checksum_valid()
{
    handler.bionicSteering = true;
    for (uint8_t c = 0; c < 30; ++c)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, 0.10, c);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());
        uint16_t sum = 0;
        for (int i = 0; i < 7; i++)
            sum += mock.sent[0].data[i];
        TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((sum + 0x73) & 0xFF), mock.sent[0].data[7]);
    }
}
```

Register them in `main()` before `return UNITY_END();`:

```cpp
    RUN_TEST(test_legacy_bionic_off_no_echo);
    RUN_TEST(test_legacy_bionic_first_frame_is_base_torque);
    RUN_TEST(test_legacy_bionic_sine_varies_over_frames);
    RUN_TEST(test_legacy_bionic_respects_perturbation_cap);
    RUN_TEST(test_legacy_bionic_hands_on_no_echo);
    RUN_TEST(test_legacy_bionic_disabled_no_echo);
    RUN_TEST(test_legacy_bionic_echo_checksum_valid);
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_legacy -v 2>&1 | tail -20`
Expected: FAIL — LegacyHandler has no `bionic` member yet (compile error on `handler.bionicSteering`/`handler.bionic`), or the bionic-off test passes but bionic-on tests fail (no echo). This is the RED state.

- [ ] **Step 4: Add the `DashBionicSteer bionic;` member + overrides to `LegacyHandler`**

In `include/handlers.h`, find `struct LegacyHandler : public CarManagerBase` (`:295`). After its existing member declarations and BEFORE `const uint32_t *filterIds() const override`, add:

```cpp
    DashBionicSteer bionic;  // bionic steering state (sine perturbation on 0x08B6 base)

    bool bionicDisabled() const override { return bionic.isDisabled(); }
    void resetBionic(uint32_t seed) override
    {
        bionic.reset();
        bionic.init(seed ? seed : 0xDEADBEEF);
    }
```

- [ ] **Step 5: Add the 0x370 bionic echo block to `LegacyHandler::handleMessage`**

In `LegacyHandler::handleMessage` (`:307`), find the line `updateHwDetectedFrom920(frame);` (near the top, after `if (onFrame) onFrame(frame);`). Insert this block IMMEDIATELY AFTER `updateHwDetectedFrom920(frame);`:

```cpp
        if (frame.id == 880 && frame.dlc >= 8)
        {
            // Bionic nag suppression (opt-in via bionicSteering; default OFF).
            uint8_t handsOn = static_cast<uint8_t>((frame.data[4] >> 6) & 0x03);
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
                uint8_t d2lo = 0x08;                       // sign nibble positive
                uint8_t d3   = 0xB6;                       // 1.80 Nm base
                bionic.applyToFrame(d2lo, d3, pert);       // sine perturbs the 0x08B6 base
                echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | d2lo);
                echo.data[3] = d3;
                echo.data[4] = static_cast<uint8_t>(frame.data[4] | 0x40);  // handsOnLevel = 1
                echo.data[5] = frame.data[5];
                uint8_t cnt = static_cast<uint8_t>(frame.data[6] & 0x0F);
                cnt = static_cast<uint8_t>((cnt + 1) & 0x0F);
                echo.data[6] = static_cast<uint8_t>((frame.data[6] & 0xF0) | cnt);
                uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] +
                               echo.data[4] + echo.data[5] + echo.data[6];
                echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

                // Self-validation (LILYGO-faithful): re-check the output checksum.
                // NOTE: by construction this is always self-consistent (echo.data[7] was
                // computed from echo.data[0..6]), so reportFailure() here is not reached
                // in production — the 3-fail auto-disable is exercised in tests via direct
                // reportFailure() calls. Kept for parity with the proven LILYGO path.
                uint16_t verify = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] +
                                  echo.data[4] + echo.data[5] + echo.data[6] + 0x73;
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
        }
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_legacy -v 2>&1 | tail -15`
Expected: PASS — all 7 new bionic tests green + existing LegacyHandler tests still green.

- [ ] **Step 7: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add include/handlers.h test/test_native_legacy/test_legacy_handler.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "feat(legacy): bionic steering DND — 0x370 sine-on-0x08B6 echo in LegacyHandler (opt-in via bionicSteering)"
```

---

### Task 2: UI 高危 warning on the bionic toggle

The `def-bionic-tgl` row exists (`mcp2515_dashboard_ui.src.h` ~1318, "仿生方向盘 实验"). Add a 「高危/严禁上车」warning chip shown when ON, mirroring `def-ntt-warn`.

**Files:**
- Modify: `include/web/mcp2515_dashboard_ui.src.h` — add `def-bionic-risk` chip + load wiring.
- Regenerate: `include/web/mcp2515_dashboard_ui.h`.

- [ ] **Step 1: Add the risk chip**

Find `def-bionic-warn` (`grep -n "def-bionic-warn" include/web/mcp2515_dashboard_ui.src.h`). Immediately after that line, add:

```html
      <div class="setting-desc" id="def-bionic-risk" style="color:#ef4444;display:none">⚠ 高危·严禁上车（动态扭矩注入，2026-06-19 事故同类）</div>
```

- [ ] **Step 2: Show/hide on load**

In `loadDefenseConfig` (find `var bio=$('def-bionic-tgl');`), after the bio load line, add:

```javascript
  var bioRisk=$('def-bionic-risk');if(bioRisk)bioRisk.style.display=!!d.bionic_steering?'block':'none';
```

- [ ] **Step 3: Regenerate ui.h + build**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
python3 scripts/minify_dashboard.py
grep -c "def-bionic-risk" include/web/mcp2515_dashboard_ui.h   # expect ≥2
pio run -e waveshare_single_can_standalone 2>&1 | tail -3        # expect SUCCESS
```

- [ ] **Step 4: Commit src.h + ui.h**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add include/web/mcp2515_dashboard_ui.src.h include/web/mcp2515_dashboard_ui.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "feat(ui): add 高危 warning chip to bionic toggle (def-bionic-risk)"
```

---

### Task 3: Contract test — bionicSteering opt-in/default-off in LegacyHandler

**Files:**
- Modify: `test/test_no_epas_nag_contract.py`.

- [ ] **Step 1: Add the contract test**

Append to `test/test_no_epas_nag_contract.py`:

```python
def test_legacy_bionic_steering_is_optin_and_gated():
    """LegacyHandler bionic (0x370 sine-on-0x08B6 echo) must be opt-in: bionicSteering
    defaults false, the bionic echo is gated on bionicSteering + handsOn==0, and the 8
    banned DashEpasNag symbols stay absent (DashBionicSteer is not one of them)."""
    import re
    from pathlib import Path
    root = Path(__file__).resolve().parents[1]
    h = (root / "include" / "handlers.h").read_text()

    # bionicSteering member defaults false (on CarManagerBase)
    assert re.search(r"Shared<bool>\s+bionicSteering\s*\{[^}]*\bfalse\b", h), \
        "bionicSteering must default to false"

    # LegacyHandler has the bionic echo gated on bionicSteering + handsOn==0
    assert "bool useBionic = (bool)bionicSteering && !bionic.isDisabled() && handsOn == 0" in h, \
        "LegacyHandler bionic echo must be gated on bionicSteering + handsOn==0"

    # bionic uses the 0x08B6 tamper base + sine (DashBionicSteer.applyToFrame)
    assert "bionic.applyToFrame(" in h
    assert "0x08B6" in (root / "include" / "dash_bionic_steer.h").read_text()

    # 8 banned DashEpasNag symbols still absent
    banned = ["DashEpasNag", "dashEpasNag", "epasNagDiag", "tryBuildEcho",
              "nextDemandTorque", "EpasNagTraceRing", "dash_epas_nag", "def_epnag"]
    for sym in banned:
        assert sym not in h, f"banned symbol {sym} must stay out of handlers.h"
```

- [ ] **Step 2: Run + commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
python3 -m pytest test/test_no_epas_nag_contract.py -v 2>&1 | tail -12
git add test/test_no_epas_nag_contract.py
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "test(contract): assert LegacyHandler bionic opt-in/gated + 8-symbol ban intact"
```

---

### Task 4: Full verification

- [ ] **Step 1: native_legacy (bionic + regression)**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_legacy -v 2>&1 | tail -10`
Expected: all PASS (7 new bionic + existing LegacyHandler tests).

- [ ] **Step 2: native_nag regression (NagHandler Phase-1 untouched)**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_nag -v 2>&1 | tail -6`
Expected: all PASS (NagHandler unchanged; this work doesn't touch it).

- [ ] **Step 3: Python contract + EPAS guard + dashboard contract**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && python3 -m pytest test/test_no_epas_nag_contract.py test/test_dashboard_api_contract.py -q 2>&1 | tail -8`
Expected: all PASS.

- [ ] **Step 4: ESP production build (LegacyHandler compiled; bionic inert unless toggled)**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone 2>&1 | tail -6`
Expected: `SUCCESS`.

- [ ] **Step 5: Fixup commit only if needed** (else skip)

---

### Task 5: Flash + on-car validation (Jordan)

This task validates the **unknown**: does Legacy EPAS tolerate the bionic, or fault like Mode-C? Bench can't answer it. **First enable in a safe area, hands ready.**

- [ ] **Step 1: Build the merged-flash asset**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
pio run -e waveshare_single_can_standalone
python3 scripts/build_release_assets.sh
```
Expected: `release-assets/merged-flash.bin` (+ 7 other standard assets).

- [ ] **Step 2: Flash the board** (MAC 28:84:85:49:93:fc; ensure the build targets Legacy — `DASH_DEFAULT_HW==0` or runtime hwMode=0)

```bash
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset write_flash --flash_mode dio \
  --flash_size keep --flash_freq 40m 0x0 release-assets/merged-flash.bin
```

- [ ] **Step 3: Smoke test** (clean boot: no panic; with bionic OFF, no 0x370 echo / no `epas_nag` lines; `canActive=NO` on bench)

```bash
python3 -c "import serial,time; s=serial.Serial('/dev/cu.usbmodemXXXX',115200); s.dtr=False; s.rts=True; time.sleep(0.1); s.rts=False; time.sleep(3); print(s.read(s.in_waiting).decode(errors='replace'))"
```

- [ ] **Step 4: On-car validation (Jordan, safe area, hands ready)**

Enable `def-bionic-tgl` on the dashboard. Observe:
1. Does the nag suppress? (functional success)
2. Any EPAS fault / body-control warning / steering anomaly? (if YES → **disable the toggle immediately** + report — incident recurring)

Report the result. If tolerated → tune amplitude/duration (`dash_bionic_steer.h`) to feel. If fault → bionic-on-Legacy is "not tolerated" (same as Mode-C); disable + remove the LegacyHandler wiring or scope to HW3/HW4.

---

## Notes for the executor

- **Bash cwd resets every call** — prefix every command with `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && ...`.
- **`pio test -e native_legacy`** compiles `LegacyHandler` directly (the test instantiates it, bypassing `app.h`'s `SelectedHandler`).
- **LegacyHandler runs in production** (unlike NagHandler) → the bionic is live in release firmware when toggled. Default OFF + gate + 3-fail-disable are the guardrails.
- **The self-validation block is LILYGO-faithful but vestigial** (always self-consistent by construction) — the 3-fail auto-disable is exercised in tests via direct `handler.bionic.reportFailure()`, not reachable in production. Kept for parity; do not rely on it for production anomaly detection.
- **LILYGO reference**: `LILYGO-T-2Can-firmware/include/handlers.h:716-838` (the NagHandler bionic — same algorithm, different host).
