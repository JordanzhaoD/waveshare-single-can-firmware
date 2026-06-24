# Soft Engage (Activation-Edge Angle Gate) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in, edge-latched "Soft Engage" angle gate to the Legacy `0x3EE bit46` FSD-activation path so the activation-edge injection waits for the wheel to near-centre (or a 5 s timeout) — aligning local V1.0.3 with upstream `flipper-tesla-fsd` v2.16-beta.10.

**Architecture:** The angle-gate decision is a **pure `inline` helper** `dashSoftEngageRelease(...)` in `can_helpers.h` (native-testable — the dashboard gate function is not compiled natively). The Legacy gate `dashLegacyFsdActivationAllowed(nowMs)` (Jordan's `hwMode=0` production path) calls the helper after the existing AP-settle check, latches the first release per AP-episode, and reads steering angle directly from `apRestoreState`. A UI toggle (`def-soft-engage-tgl`) + NVS (`def_se`) + `/defense_config` + `/status` expose it, mirroring the proven `def_ntt`/`dashNagTorqueTamper` pattern.

**Tech Stack:** ESP-IDF + PlatformIO (ESP32-S3); TWAI CAN; Unity native tests; Python `pytest` contract tests; ArduinoJson; minified dashboard UI (`mcp2515_dashboard_ui.src.h` → `ui.h` via `scripts/minify_dashboard.py`).

**Spec:** `docs/superpowers/specs/2026-06-25-soft-engage-alignment-design.md` (rev. 3).

**Safety constraints (do not violate):**
- ⛔ The 8 banned `DashEpasNag` symbols must stay banned (`test_no_epas_nag_contract.py`). This work is entirely `0x3EE`-side — it touches no EPAS/`0x370` code.
- Soft Engage only **delays** an existing bit46 assertion; it never injects anything new. Toggle OFF = exact V1.0.3 behaviour.
- Git: commit with `git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local"`. Stay on local `main`; do NOT push unless Jordan says so.
- Do NOT commit `ui.h` build-stamp churn that isn't a real content change — but here Task 6 deliberately regenerates `ui.h` (real content change), so commit it.
- IDE `clang` errors on `unity.h`/`Arduino.h` are false positives — `pio test` / `pio run` are source of truth.

---

### Task 1: Pure helper `dashSoftEngageRelease` + native unit tests (TDD core)

This is the only natively-unit-testable piece (the real gate lives in the ESP-only dashboard header). Full TDD.

**Files:**
- Modify: `include/can_helpers.h:53-54` (insert helper after `nagTorqueTamperRuntime`, before the blank line at `:54`)
- Modify: `test/test_native_helpers/test_helpers.cpp` (append 9 tests + `RUN_TEST` lines in `main()`)

- [ ] **Step 1: Write the 9 failing tests** (append before `int main()` in `test/test_native_helpers/test_helpers.cpp`)

```cpp
// --- dashSoftEngageRelease (Soft Engage angle gate) ---

void test_softEngageRelease_disabled_bypasses()
{
    // toggle OFF → always release (V1.0.3 behaviour) regardless of angle
    TEST_ASSERT_TRUE(dashSoftEngageRelease(false, false, true, 0, 100,
                                           true, false, 50));
}

void test_softEngageRelease_already_sent_latches()
{
    // already activated this episode → ignore angle (mid-corner safe)
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, true, true, 0, 300,
                                           true, false, 50));
}

void test_softEngageRelease_not_settle_defers_to_settle_gate()
{
    // settle gate not yet passed → not soft-engage's job → release=true
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 300,
                                           false, false, 50));
}

void test_softEngageRelease_centred_releases()
{
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 0,
                                           true, false, 50));
}

void test_softEngageRelease_off_centre_holds()
{
    // |angle|=100 > 50, no timeout → HOLD (the core behaviour)
    TEST_ASSERT_FALSE(dashSoftEngageRelease(true, false, true, 0, 100,
                                            true, false, 50));
}

void test_softEngageRelease_off_centre_timeout_releases()
{
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 100,
                                           true, true, 50));
}

void test_softEngageRelease_unseen_angle_holds()
{
    // steerSeen=false (no 0x129 yet) → hold until timeout
    TEST_ASSERT_FALSE(dashSoftEngageRelease(true, false, false, 0, 0,
                                            true, false, 50));
}

void test_softEngageRelease_invalid_validity_holds()
{
    // steerValidity != 0 → signal invalid → hold until timeout
    TEST_ASSERT_FALSE(dashSoftEngageRelease(true, false, true, 1, 0,
                                            true, false, 50));
}

void test_softEngageRelease_threshold_boundary_is_inclusive()
{
    // |angle| == threshold (50) → centred (<= inclusive)
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 50,
                                           true, false, 50));
}
```

And register them inside `main()` (after the last existing `RUN_TEST(test_runtime_defaults_start_disabled);` and before `return UNITY_END();`):

```cpp
    RUN_TEST(test_softEngageRelease_disabled_bypasses);
    RUN_TEST(test_softEngageRelease_already_sent_latches);
    RUN_TEST(test_softEngageRelease_not_settle_defers_to_settle_gate);
    RUN_TEST(test_softEngageRelease_centred_releases);
    RUN_TEST(test_softEngageRelease_off_centre_holds);
    RUN_TEST(test_softEngageRelease_off_centre_timeout_releases);
    RUN_TEST(test_softEngageRelease_unseen_angle_holds);
    RUN_TEST(test_softEngageRelease_invalid_validity_holds);
    RUN_TEST(test_softEngageRelease_threshold_boundary_is_inclusive);
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native -f test_native_helpers -v 2>&1 | tail -20`
Expected: COMPILE ERROR — `error: use of undeclared identifier 'dashSoftEngageRelease'` (helper not defined yet).

- [ ] **Step 3: Implement the helper** (insert into `include/can_helpers.h` between line 53 `nagTorqueTamperRuntime` and the blank line at 54, i.e. immediately before `inline bool enhancedAutopilotInjectionAllowed`)

```cpp
// Pure Soft Engage angle-gate decision (native-testable; no state).
// The Legacy dashboard gate dashLegacyFsdActivationAllowed() calls this once
// AP-settle is being evaluated. Returns true iff the Legacy 0x3EE bit46
// activation may fire NOW; false means hold bit46 off until the wheel nears
// centre (|steerAngleX10| <= angleThreshX10 AND steerValidity==0) or the
// timeout elapses. Mirrors upstream flipper-tesla-fsd v2.16-beta.10 Soft Engage.
// enabled=false or alreadySent=true or settled=false → true (not our job).
inline bool dashSoftEngageRelease(bool enabled, bool alreadySent,
                                  bool steerSeen, uint8_t steerValidity,
                                  int16_t steerAngleX10,
                                  bool settled, bool timeout,
                                  int angleThreshX10)
{
    if (!enabled)    return true;   // toggle OFF → V1.0.3 behaviour
    if (alreadySent) return true;   // latched this episode → ignore angle
    if (!settled)    return true;   // settle gate hasn't passed (not our job)
    const bool centred = steerSeen
                         && steerValidity == 0
                         && abs(static_cast<int>(steerAngleX10)) <= angleThreshX10;
    return centred || timeout;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native -f test_native_helpers -v 2>&1 | tail -15`
Expected: PASS — all 9 new tests green, plus the existing helpers tests still green.

- [ ] **Step 5: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add include/can_helpers.h test/test_native_helpers/test_helpers.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "feat(soft-engage): add pure dashSoftEngageRelease helper + native tests"
```

---

### Task 2: Dashboard gate integration (constants + state + gate call)

ESP-only code (the dashboard header is `#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)`). Validated by ESP build (Task 7) + Python contract (Task 5), not native unit test.

**Files:**
- Modify: `include/web/mcp2515_dashboard.h:172-178` (constants + state), `:963-969` (gate tail)

- [ ] **Step 1: Add constants** (insert after line 174 `static constexpr uint32_t kLegacyFsdActivationSettleMaxMs = 3000;`)

```cpp
// Soft Engage (upstream flipper-tesla-fsd v2.16-beta.10 alignment): once AP
// has settled, additionally hold the Legacy 0x3EE bit46 activation-edge
// injection until the steering wheel is near-centred (or the timeout fires),
// then latch for the rest of the episode. Reduces curve-entry jerk.
static constexpr bool kSoftEngageDefaultEnabled = true;     // ON: Jordan is the 8.3.6 jerk owner
static constexpr int SOFT_ENGAGE_ANGLE_THRESH_X10 = 50;     // ±5.0° (conservative; on-car tune)
static constexpr uint32_t SOFT_ENGAGE_TIMEOUT_MS = 5000;    // long-curve fallback (never strand driver)
```

- [ ] **Step 2: Add state statics** (insert after line 178 `static bool legacyFsdLastAllowed = false;`)

```cpp
static bool dashSoftEngage = kSoftEngageDefaultEnabled;     // opt-in toggle (UI/NVS `def_se`)
static bool legacySoftEngageSent = false;                   // per-episode latch: first bit46 release
```

- [ ] **Step 3: Rewrite the gate tail** — replace lines 963-969 of `dashLegacyFsdActivationAllowed`:

Existing (`:963-969`):
```cpp
    if (legacyFsdApActiveSinceMs == 0)
        legacyFsdApActiveSinceMs = nowMs;
    bool stable = (nowMs - legacyFsdApActiveSinceMs) >= legacyFsdRequiredStableMs;
    legacyFsdLastAllowed = stable;
    if (!stable)
        legacyFsdLastBlockedMs = nowMs;
    return stable;
```

Replace with:
```cpp
    if (legacyFsdApActiveSinceMs == 0)
    {
        legacyFsdApActiveSinceMs = nowMs;
        legacySoftEngageSent = false; // new AP episode → re-arm soft-engage latch
    }
    const bool stable = (nowMs - legacyFsdApActiveSinceMs) >= legacyFsdRequiredStableMs;
    const bool timeout = (nowMs - legacyFsdApActiveSinceMs)
                         >= (legacyFsdRequiredStableMs + SOFT_ENGAGE_TIMEOUT_MS);
    const bool release = dashSoftEngageRelease(dashSoftEngage, legacySoftEngageSent,
                                               apRestoreState.steerSeen,
                                               apRestoreState.steerValidity,
                                               apRestoreState.steerAngleX10,
                                               stable, timeout, SOFT_ENGAGE_ANGLE_THRESH_X10);
    if (stable && !release)
    {
        legacyFsdLastAllowed = false;
        legacyFsdLastBlockedMs = nowMs;
        return false; // Soft Engage hold: wheel off-centre, within timeout window
    }
    if (stable)
        legacySoftEngageSent = true; // latch: angle ignored for rest of episode
    legacyFsdLastAllowed = stable;
    if (!stable)
        legacyFsdLastBlockedMs = nowMs;
    return stable;
```

- [ ] **Step 4: Smoke-build the ESP target to confirm it compiles**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone 2>&1 | tail -5`
Expected: `SUCCESS` (RAM/Flash %). If compile error, fix before continuing.

- [ ] **Step 5: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add include/web/mcp2515_dashboard.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "feat(soft-engage): gate Legacy bit46 on near-centre wheel in dashLegacyFsdActivationAllowed"
```

---

### Task 3: NVS persistence (`def_se`)

Mirror `def_ntt` / `dashNagTorqueTamper` exactly, substituting names.

**Files:**
- Modify: `include/web/mcp2515_dashboard.h` — save block near `:1381`; load block near `:1585`

- [ ] **Step 1: Add NVS save** — after line 1381 `prefs.putBool("def_ntt", dashNagTorqueTamper);` add:

```cpp
    prefs.putBool("def_se", dashSoftEngage);
```

- [ ] **Step 2: Add NVS load** — after line 1585 `dashNagTorqueTamper = prefs.getBool("def_ntt", false);` (and after the `:1586` `nagTorqueTamperRuntime = dashNagTorqueTamper;` line) add:

```cpp
    dashSoftEngage = prefs.getBool("def_se", kSoftEngageDefaultEnabled);
```

(No runtime-global sync needed — the gate reads `dashSoftEngage` directly.)

- [ ] **Step 3: Build to confirm**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone 2>&1 | tail -3`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add include/web/mcp2515_dashboard.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "feat(soft-engage): persist def_se toggle in NVS (load default ON)"
```

---

### Task 4: HTTP surface (`/defense_config` GET+POST, `/status`, settings bundle)

Mirror the `nag_torque_tamper` / `nagTorqueTamper` fields exactly.

**Files:**
- Modify: `include/web/mcp2515_dashboard.h` — `:3093` (GET JSON), `:3125` (POST hasArg list), `:3147` (POST parse), `:5548`+`:5610`+`:5746`+`:6028` (settings bundle)

- [ ] **Step 1: GET JSON** — in `dashDefenseConfigJson()`, after line 3094 (`j += dashNagTorqueTamper ? "true" : "false";`) add:

```cpp
    j += ",\"soft_engage\":";
    j += dashSoftEngage ? "true" : "false";
```

- [ ] **Step 2: POST hasArg list** — in `handleDefenseConfig()`, edit line 3125 `server.hasArg("nag_torque_tamper"))` to also include the new arg.

Change:
```cpp
        server.hasArg("nag_torque_tamper"))
```
To:
```cpp
        server.hasArg("nag_torque_tamper") ||
        server.hasArg("soft_engage"))
```

- [ ] **Step 3: POST parse** — after the `nag_torque_tamper` parse block (lines 3147-3154) add:

```cpp
        if (server.hasArg("soft_engage"))
        {
            bool v = dashArgTruthy(server.arg("soft_engage"));
            dashSoftEngage = v; // gate reads this directly on the next Legacy mux0 frame
        }
```

- [ ] **Step 4: Settings bundle — local declaration** — after line 5548 `bool storedNagTorqueTamper = dashNagTorqueTamper;` add:

```cpp
    bool storedSoftEngage = dashSoftEngage;
```

- [ ] **Step 5: Settings bundle — NVS-backed load** — after line 5610 `storedNagTorqueTamper = p.getBool("def_ntt", dashNagTorqueTamper);` add:

```cpp
        storedSoftEngage = p.getBool("def_se", dashSoftEngage);
```

- [ ] **Step 6: Settings bundle — /status export** — after line 5746 `j += ",\"nagTorqueTamper\":" + String(storedNagTorqueTamper ? "true" : "false");` add:

```cpp
    j += ",\"softEngage\":" + String(storedSoftEngage ? "true" : "false");
```

- [ ] **Step 7: Settings bundle — import** — after line 6028 `p.putBool("def_ntt", defense["nagTorqueTamper"].as<bool>());` add:

```cpp
        if (defense["softEngage"].is<bool>())
            p.putBool("def_se", defense["softEngage"].as<bool>());
```

- [ ] **Step 8: Build to confirm**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone 2>&1 | tail -3`
Expected: `SUCCESS`.

- [ ] **Step 9: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add include/web/mcp2515_dashboard.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "feat(soft-engage): expose soft_engage via /defense_config + /status + settings bundle"
```

---

### Task 5: Python contract test (verifies ESP-only wiring)

The dashboard gate isn't natively unit-testable, so a source-contract test (mirroring `test_dashboard_api_contract.py`) pins the wiring.

**Files:**
- Modify: `test/test_dashboard_api_contract.py`

- [ ] **Step 1: Read the existing test structure**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && grep -n "def test_\|dashLegacyFsdActivationAllowed\|nag_torque_tamper\|def_ntt" test/test_dashboard_api_contract.py | head -30`
to see how existing assertions read source files (paths + substring checks). Follow that exact style.

- [ ] **Step 2: Add a new test function** (place it next to the other dashboard-source assertions; use the same source-reading helper the file already uses)

```python
def test_soft_engage_is_wired_into_legacy_gate():
    """Soft Engage angle gate must be wired into the Legacy dashboard gate and
    exposed via NVS + HTTP, mirroring the spec (rev. 3). Pure helper lives in
    can_helpers.h; the ESP-only dashboard gate calls it and reads apRestoreState."""
    from pathlib import Path
    can_helpers = Path('include/can_helpers.h').read_text()
    dash = Path('include/web/mcp2515_dashboard.h').read_text()

    # 1. pure helper is defined in can_helpers.h
    assert 'dashSoftEngageRelease(' in can_helpers

    # 2. gate calls the helper + reads steering angle + manages the latch
    assert 'dashSoftEngageRelease(' in dash
    assert 'apRestoreState.steerSeen' in dash
    assert 'apRestoreState.steerValidity' in dash
    assert 'apRestoreState.steerAngleX10' in dash
    assert 'legacySoftEngageSent' in dash
    # latch re-arms when the settle timer resets (new AP episode)
    assert 'legacySoftEngageSent = false' in dash

    # 3. constants + state exist
    assert 'SOFT_ENGAGE_ANGLE_THRESH_X10' in dash
    assert 'SOFT_ENGAGE_TIMEOUT_MS' in dash
    assert 'kSoftEngageDefaultEnabled' in dash
    assert 'dashSoftEngage' in dash

    # 4. NVS key + HTTP fields
    assert '"def_se"' in dash
    assert '"soft_engage"' in dash
    assert '"softEngage"' in dash
```

(If the file already imports `Path` or uses a helper variable for the dashboard source, reuse that instead of re-reading — keep the assertions identical.)

- [ ] **Step 3: Run the contract test**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && python3 -m pytest test/test_dashboard_api_contract.py -k soft_engage -v`
Expected: PASS (all assertions hold after Tasks 2-4). If any fail, the corresponding wiring from Tasks 2-4 is missing — fix it.

- [ ] **Step 4: Run the full dashboard contract suite (regression)**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && python3 -m pytest test/test_dashboard_api_contract.py -v 2>&1 | tail -15`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add test/test_dashboard_api_contract.py
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "test(contract): assert Soft Engage wiring in Legacy gate + NVS/HTTP surface"
```

---

### Task 6: UI toggle `def-soft-engage-tgl` + regenerate `ui.h`

Place the HTML row in the AP-First card (semantic home for an activation-gate refinement); wire it through the proven `saveDefenseConfig` / `loadDefenseConfig` / `/defense_config` path (functions query by id, independent of card).

**Files:**
- Modify: `include/web/mcp2515_dashboard_ui.src.h` — HTML row (after the `ap-core-gate-tgl` row), `loadDefenseConfig` (`:2425`-area), `saveDefenseConfig` (`:2744` + `:2754`-area). **Do NOT** add to `experimentSummary` (`:1889`) — Soft Engage is default-ON and not a high-risk experiment.
- Regenerate: `include/web/mcp2515_dashboard_ui.h` via `scripts/minify_dashboard.py`

- [ ] **Step 1: Add the HTML row** — find the `ap-core-gate-tgl` row (`grep -n "ap-core-gate-tgl" include/web/mcp2515_dashboard_ui.src.h`) and insert a new `setting-row` immediately AFTER its containing block's closing `</div>`. Use this exact block:

```html
  <div class="setting-row">
    <div>
      <div class="setting-name">Soft Engage 方向盘居中</div>
      <div class="setting-desc">激活时 hold 到方向盘近居中再注入，降弯道猛甩（超时 5s 兜底）</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-soft-engage-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
```

- [ ] **Step 2: Load path** — in `loadDefenseConfig`, after line 2425 (the `var nttWarn=...;` line) add:

```javascript
  var se=$('def-soft-engage-tgl');if(se)se.checked=!!d.soft_engage;
```

- [ ] **Step 3: Save path — var decl** — in `saveDefenseConfig`, after line 2744 `var ntt=$('def-ntt-tgl');` add:

```javascript
  var se=$('def-soft-engage-tgl');
```

- [ ] **Step 4: Save path — payload field** — in the `data={...}` object, after line 2754 `nag_torque_tamper:ntt&&ntt.checked?'1':'0',` add:

```javascript
    soft_engage:se&&se.checked?'1':'0',
```

- [ ] **Step 5: Regenerate the minified UI header**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && python3 scripts/minify_dashboard.py`
Expected: `include/web/mcp2515_dashboard_ui.h` rewritten (the script reads `mcp2515_dashboard_ui.src.h` and emits `mcp2515_dashboard_ui.h`).

- [ ] **Step 6: Build to confirm the new UI compiles into the firmware**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone 2>&1 | tail -3`
Expected: `SUCCESS`. (If `ui.h` also picked up a build-stamp churn from the `extra_scripts` hook, that's fine here — the content genuinely changed, so commit it.)

- [ ] **Step 7: Commit src.h + regenerated ui.h together**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add include/web/mcp2515_dashboard_ui.src.h include/web/mcp2515_dashboard_ui.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "feat(ui): add Soft Engage toggle (def-soft-engage-tgl) under AP-First card"
```

---

### Task 7: Full verification (native + contract + guard + ESP build)

- [ ] **Step 1: Native helper tests**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native -f test_native_helpers -v 2>&1 | tail -10`
Expected: all PASS (9 new + existing).

- [ ] **Step 2: V1.0.3 regression (handler stub tests — must be unaffected)**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_injection_after_ap -v 2>&1 | tail -10`
Expected: all PASS (these use stub callbacks; Soft Engage default-ON does not touch them).

- [ ] **Step 3: Python contract + EPAS guard**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
python3 -m pytest test/test_dashboard_api_contract.py test/test_no_epas_nag_contract.py -v 2>&1 | tail -20
```
Expected: all PASS. The EPAS guard must remain green (this work adds no EPAS symbols).

- [ ] **Step 4: Full ESP production build**

Run: `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone 2>&1 | tail -6`
Expected: `SUCCESS` with RAM/Flash %. Note the build hash for Task 8.

- [ ] **Step 5: Final fixup commit (only if steps 1-4 needed fixes; otherwise skip)**

If steps 1-4 needed no fixes, this task has no commit (verification only). If you fixed anything, commit with `fix(soft-engage): ...`.

---

### Task 8: Flash merged asset + smoke test (on-car prep)

Jordan signed off "实现+烧板实车测试" for this track. This task flashes the board and smoke-tests; the on-car driving validation is Jordan's.

- [ ] **Step 1: Build the merged-flash asset**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
ls .pio/build/waveshare_single_can_standalone/firmware.bin   # confirm it exists
python3 -m esptool --chip esp32s3 merge_bin -o scratch/soft-engage-merged.bin \
  --flash_mode dio --flash_size keep --flash_freq 40m \
  0x0 .pio/build/waveshare_single_can_standalone/bootloader.bin \
  0x8000 .pio/build/waveshare_single_can_standalone/partitions.bin \
  0x19000 .pio/build/waveshare_single_can_standalone/ota_data_initial.bin \
  0x20000 .pio/build/waveshare_single_can_standalone/firmware.bin
```
Expected: `scratch/soft-engage-merged.bin` written. (If PlatformIO placed `bootloader.bin` elsewhere: `find .pio/build/waveshare_single_can_standalone -name 'bootloader.bin'`.)

- [ ] **Step 2: Flash the board**

Run (replace `/dev/cu.usbmodem*` with the board's actual port; the board MAC is `28:84:85:49:93:fc`):
```bash
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem2101 -b 460800 \
  --before default_reset --after hard_reset write_flash --flash_mode dio \
  --flash_size keep --flash_freq 40m 0x0 scratch/soft-engage-merged.bin
```
Expected: `Hash of data verified.` + `Hard resetting via RTS pin...`.

- [ ] **Step 3: Smoke-test the boot log**

Run (capture serial; `pyserial` with explicit DTR/RTS reset per the known ESP32-S3 USB-CDC gotcha):
```bash
python3 -c "import serial,time; s=serial.Serial('/dev/cu.usbmodem2101',115200); s.dtr=False; s.rts=True; time.sleep(0.1); s.rts=False; time.sleep(3); print(s.read(s.in_waiting).decode(errors='replace'))"
```
Expected: clean boot — `App version v1.0...`, **no panic**, **no `epas_nag` log lines**, `canActive=NO` (bench, no car). No `Guru Meditation` / `abort()`.

- [ ] **Step 4: Verify the toggle is live over HTTP**

With the board on WiFi (or its AP), `curl http://<board-ip>/defense_config` → expect `"soft_engage":true` in the JSON. `curl http://<board-ip>/status` → expect `"softEngage":true` in the `defense` block.

- [ ] **Step 5: Report to Jordan**

Summarise: gate wired + tests green + build SUCCESS + flashed MAC `28:84:85:49:93:fc` + smoke clean + `soft_engage:true` live. Hand off for on-car validation per spec §7 (straight road = no extra hold; curve = brief hold then engage on straightening or at 5 s timeout; tune threshold/timeout to feel). Flag the two on-car unknowns from spec §8: the `steerValidity==0` assumption and the ±5° threshold.

---

## Notes for the executor

- **Bash cwd resets every call** — prefix every `pio`/`pytest` with `cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && ...` or use absolute paths.
- **`python3 -m unittest test.test_xxx` does NOT work** (no `__init__.py`) — use `pytest` or `python3 -m pytest`.
- **`esptool` subcommand is `merge_bin`** (underscore), v4.8.1.
- **ESP32-S3 USB-CDC re-enumerates on reset** — use `esptool ... --after hard_reset` then reconnect `pyserial` with explicit DTR/RTS to catch the boot log.
- Every backend touchpoint is a 1:1 mirror of `def_ntt` / `dashNagTorqueTamper` / `nag_torque_tamper` / `nagTorqueTamper` — when in doubt, grep that pattern and copy its shape, substituting names.
