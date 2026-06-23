# Steer-Jerk AP-Injection Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the AP-First gate's two failure modes that let Legacy `0x3EE` FSD-enable injection land on the AP activation edge (the steer-jerk root cause on CN 2026.8.3.6 HW3), and warn affected users.

**Architecture:** Two surgical changes. (1) `isDASAutopilotActive` must recognize DAS state 6 — the engage state CN 2026.8.3.6 uses — so `APActive` goes true and the existing AP-First settle gate can arm. (2) Enable `INJECTION_AFTER_AP` in the waveshare build so the (already-built, already-UI-exposed) gate + delay default ON, moving injection from the +0.2 s activation edge to +2 s stable. No new injection; EPAS-nag ban preserved. Plus docs + UI warnings that the fix reduces (not eliminates) the jerk.

**Tech Stack:** ESP-IDF + PlatformIO (native Unity tests + Python unittest contracts), ArduinoJson, TWAI CAN. Minify pipeline (`scripts/minify_dashboard.py`) regenerates `mcp2515_dashboard_ui.h` from `mcp2515_dashboard_ui.src.h`.

**Spec:** `docs/superpowers/specs/2026-06-23-steer-jerk-ap-injection-fix-design.md`
**Diagnosis/data:** `docs/steer-jerk-diagnosis-20260623.md`, `scratch/steer-jerk/`

**Git note:** Repo has no `user.name`/`user.email` configured. Every commit uses:
`git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit ...`. Run all commands from repo root (`/Users/ziwind/my-vibe-project/waveshare-single-can-firmware`). Branch `main` matches the repo's established workflow.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `include/can_helpers.h` | DAS/AP state decoders (`isDASAutopilotActive`) | Modify line 77: `<= 5` → `<= 6` |
| `test/test_native_helpers/test_helpers.cpp` | Unity tests for can_helpers | Add 2 tests + 2 `RUN_TEST` |
| `platformio_profile.example.h` | Factory build profile (copied to `platformio_profile.h`) | Uncomment `#define INJECTION_AFTER_AP` (line 24) |
| `.github/workflows/tests.yml` | CI build matrix | Add `--enable INJECTION_AFTER_AP` to `platformio-build` profile |
| `test/test_dashboard_api_contract.py` | Python contract tests | Add gate-default-ON contract test |
| `README.md` | Project docs | Add CN 2026.8.3.6 safety warning section |
| `include/web/mcp2515_dashboard_ui.src.h` | Dashboard UI source | Add 2026.8.3.6 warning strip in `ap-core-card` |
| `include/web/mcp2515_dashboard_ui.h` | Minified UI (embedded) | Regenerate via `minify_dashboard.py` |

---

## Task 1: Recognize DAS state 6 as AP-active (TDD)

**Files:**
- Modify: `include/can_helpers.h:70-78`
- Test: `test/test_native_helpers/test_helpers.cpp` (add tests after line 163, register after line 305)

- [ ] **Step 1: Write the failing tests**

In `test/test_native_helpers/test_helpers.cpp`, immediately after the existing `test_isDASAutopilotActive_false_for_available_state` function (ends ~line 163), add:

```cpp
void test_isDASAutopilotActive_true_for_state_6()
{
    // CN 2026.8.3.6 engages AP to DAS state 6; it must count as active so the
    // AP-First settle gate arms. Regression for the steer-jerk root cause
    // (docs/superpowers/specs/2026-06-23-steer-jerk-ap-injection-fix-design.md).
    TEST_ASSERT_TRUE(isDASAutopilotActive(6));
}

void test_isDASAutopilotActive_false_for_fault_and_handover_states()
{
    // State 8 (handover/warning) and 9 (fault) must NOT count as active.
    TEST_ASSERT_FALSE(isDASAutopilotActive(8));
    TEST_ASSERT_FALSE(isDASAutopilotActive(9));
}
```

- [ ] **Step 2: Register the new tests**

In the same file, immediately after line 305 (`RUN_TEST(test_isDASAutopilotActive_false_for_available_state);`), add:

```cpp
    RUN_TEST(test_isDASAutopilotActive_true_for_state_6);
    RUN_TEST(test_isDASAutopilotActive_false_for_fault_and_handover_states);
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio test -e native_helpers`
Expected: `test_isDASAutopilotActive_true_for_state_6` FAILS (`Expected TRUE Was FALSE`), because the current implementation only accepts states 3–5. The fault-state test passes already.

- [ ] **Step 4: Implement the fix**

In `include/can_helpers.h`, replace the body of `isDASAutopilotActive` (lines 75–78):

```c
inline bool isDASAutopilotActive(uint8_t status)
{
    // DAS_autopilotState (0x399 byte0 low nibble). AP is actively engaged on
    // states 3-5 (older firmware) and 6 (newer firmware incl. CN 2026.8.3.6).
    // State 8 (handover/warning) and 9 (fault) are NOT active.
    return status >= 3 && status <= 6;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native_helpers`
Expected: all tests PASS, including the two new ones.

- [ ] **Step 6: Commit**

```bash
git add include/can_helpers.h test/test_native_helpers/test_helpers.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "fix(can): isDASAutopilotActive recognizes DAS state 6 (CN 2026.8.3.6)

CN 2026.8.3.6 HW3 engages AP to DAS state 6, but the helper only accepted
3-5, leaving APActive permanently false and disarming the AP-First settle
gate (steer-jerk root cause). Add state 6; 8/9 stay inactive."
```

---

## Task 2: Enable AP-First gate by default + contract test (TDD)

**Files:**
- Modify: `platformio_profile.example.h:24`
- Modify: `.github/workflows/tests.yml:89-90`
- Test: `test/test_dashboard_api_contract.py` (add one test method)

- [ ] **Step 1: Write the failing contract test**

In `test/test_dashboard_api_contract.py`, add this method to the existing test class (place it next to `test_waveshare_single_can_standalone_env_is_declared`, ~line 185 — same class, same indentation):

```python
    def test_waveshare_build_enables_injection_after_ap_gate(self) -> None:
        """AP-First gate must default ON for the waveshare build so Legacy 0x3EE
        injection is held off the AP activation edge (steer-jerk root cause).
        See docs/superpowers/specs/2026-06-23-steer-jerk-ap-injection-fix-design.md."""
        import re
        profile = (ROOT / "platformio_profile.example.h").read_text(encoding="utf-8")
        # Must be uncommented (not the factory '// #define ...' form):
        self.assertIsNotNone(
            re.search(r"^#define INJECTION_AFTER_AP\s*$", profile, re.M),
            "platformio_profile.example.h must '#define INJECTION_AFTER_AP' (uncommented)",
        )
        self.assertNotIn("// #define INJECTION_AFTER_AP", profile)
        workflow = (ROOT / TESTS_WORKFLOW).read_text(encoding="utf-8")
        self.assertIn("--enable INJECTION_AFTER_AP", workflow)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m unittest test.test_dashboard_api_contract.DashboardApiContractTests.test_waveshare_build_enables_injection_after_ap_gate -v`
Expected: FAIL — regex not found (`INJECTION_AFTER_AP` is still commented in the profile and absent from the workflow).

- [ ] **Step 3: Enable the flag in the factory profile**

In `platformio_profile.example.h`, change line 24 from:

```c
// #define INJECTION_AFTER_AP
```
to:

```c
#define INJECTION_AFTER_AP
```

- [ ] **Step 4: Enable the flag in CI**

In `.github/workflows/tests.yml`, change the `platformio-build` matrix profile (lines 89–90) from:

```yaml
            profile: --driver DRIVER_TWAI --vehicle HW4 --enable EMERGENCY_VEHICLE_DETECTION
              --enable ENHANCED_AUTOPILOT
```
to:

```yaml
            profile: --driver DRIVER_TWAI --vehicle HW4 --enable EMERGENCY_VEHICLE_DETECTION
              --enable ENHANCED_AUTOPILOT --enable INJECTION_AFTER_AP
```

- [ ] **Step 5: Run the contract test to verify it passes**

Run: `python3 -m unittest test.test_dashboard_api_contract -v 2>&1 | tail -5`
Expected: all tests PASS, including the new one.

- [ ] **Step 6: Verify the profile script accepts the flag and produces it**

Run:
```bash
cp platformio_profile.example.h platformio_profile.h
python3 scripts/platformio_set_profile.py --driver DRIVER_TWAI --vehicle HW4 \
  --enable EMERGENCY_VEHICLE_DETECTION --enable ENHANCED_AUTOPILOT --enable INJECTION_AFTER_AP
grep INJECTION_AFTER_AP platformio_profile.h
```
Expected: one uncommented line `#define INJECTION_AFTER_AP` (no leading `//`).

- [ ] **Step 7: Commit**

```bash
git add platformio_profile.example.h .github/workflows/tests.yml test/test_dashboard_api_contract.py
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "feat(build): enable INJECTION_AFTER_AP so AP-First gate defaults ON

Holds Legacy 0x3EE FSD-enable injection until AP is stably engaged for
ap_delay_ms (default 2000ms), moving it off the +0.2s activation edge
that triggers the steer-jerk on CN 2026.8.3.6. Factory profile + CI both
set the flag; user can still disable via the ap-core-gate-tgl toggle."
```

---

## Task 3: Full safety + build verification

**Files:** none modified (verification only).

- [ ] **Step 1: Native helpers tests (state-6 fix)**

Run: `pio test -e native_helpers`
Expected: all PASS (incl. `test_isDASAutopilotActive_true_for_state_6`).

- [ ] **Step 2: AP-First native tests (gate feature intact)**

Run: `pio test -e native_injection_after_ap`
Expected: all PASS.

- [ ] **Step 3: EPAS-nag ban contract (must stay green — no 0x370)**

Run: `python3 -m pytest test/test_no_epas_nag_contract.py -q`
Expected: PASS — the 8 forbidden symbols (`DashEpasNag`, `dashEpasNag`, `epasNagDiag`, `tryBuildEcho`, `nextDemandTorque`, `EpasNagTraceRing`, `dash_epas_nag`, `def_epnag`) remain absent.

- [ ] **Step 4: Full dashboard API contract suite**

Run: `python3 -m unittest test.test_dashboard_api_contract 2>&1 | tail -5`
Expected: OK / all PASS.

- [ ] **Step 5: Firmware build with the new flag (compiles + gate default on)**

Run:
```bash
cp platformio_profile.example.h platformio_profile.h
python3 scripts/platformio_set_profile.py --driver DRIVER_TWAI --vehicle HW4 \
  --enable EMERGENCY_VEHICLE_DETECTION --enable ENHANCED_AUTOPILOT --enable INJECTION_AFTER_AP
SKIP_DASH_CREDENTIAL_CHECK=1 pio run -e waveshare_single_can_standalone
```
Expected: `SUCCESS` (RAM/Flash percentages reported). This confirms `INJECTION_AFTER_AP` compiles and `DASH_AP_GATE_DEFAULT` resolves to `true`.

- [ ] **Step 6: Restore the build-id stamp on ui.h (do not commit it)**

The build in Step 5 runs `update_ota_build_timestamp.py`, which stamps `DASH_UI_BUILD_ID` into `include/web/mcp2515_dashboard_ui.h`. Per repo convention this stamp is never committed. Run:

```bash
git restore include/web/mcp2515_dashboard_ui.h
git status --short include/web/mcp2515_dashboard_ui.h
```
Expected: no output (file clean). If Task 5 (UI warning) will run next, this is re-minified there anyway.

- [ ] **Step 7: No commit** (verification-only task). If any step failed, fix and re-run before proceeding.

---

## Task 4: Docs warning for CN 2026.8.3.6

**Files:**
- Modify: `README.md` (extend the existing `### 安全` section at line 163)

- [ ] **Step 1: Append the 2026.8.3.6 warning to the existing 安全 section**

In `README.md`, the `### 安全` section (line 163) currently ends with this blockquote (line 165) followed by a `---` rule (line 167):

```markdown
> ⚠️ **重要**：对车辆 CAN 总线进行任何修改都存在风险。CAN 总线涉及转向、制动、安全气囊等安全关键系统。请仅在充分了解相关报文含义、并在合规、可控（如台架或封闭场地）环境下使用。本项目仅供研究学习，使用者自行承担一切风险与法律责任。详见 [DISCLAIMER.md](DISCLAIMER.md)。
```

Replace that blockquote line with the same blockquote plus a new 2026.8.3.6 blockquote immediately after it:

```markdown
> ⚠️ **重要**：对车辆 CAN 总线进行任何修改都存在风险。CAN 总线涉及转向、制动、安全气囊等安全关键系统。请仅在充分了解相关报文含义、并在合规、可控（如台架或封闭场地）环境下使用。本项目仅供研究学习，使用者自行承担一切风险与法律责任。详见 [DISCLAIMER.md](DISCLAIMER.md)。

> ⚠️ **China 2026.8.3.6 HW3 高风险**：该固件收紧了自动驾驶预检。AP 激活边沿注入 FSD-enable（`0x3EE`）可能触发**方向盘猛甩 + 故障退出**。本固件默认开启 AP-First 门控（注入延迟到 AP 稳定 ~2s 后），**降低但无法消除**该风险（残留 <5%）。建议先用 **Listen-Only 模式**验证。详见 `docs/steer-jerk-diagnosis-20260623.md`。
```

- [ ] **Step 2: Verify the README has the warning**

Run: `grep -n "2026.8.3.6" README.md`
Expected: at least one match.

- [ ] **Step 3: Commit**

```bash
git add README.md
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "docs(safety): warn CN 2026.8.3.6 steer-jerk risk + Listen-Only guidance"
```

---

## Task 5: UI warning banner in the AP-injection card

**Files:**
- Modify: `include/web/mcp2515_dashboard_ui.src.h:823` (add a warning strip after the existing `safety-strip`)
- Regenerate: `include/web/mcp2515_dashboard_ui.h`

- [ ] **Step 1: Add the warning strip to the UI source**

In `include/web/mcp2515_dashboard_ui.src.h`, locate the existing fail-closed `safety-strip` inside `ap-core-card` (line 823):

```
          <div class="safety-strip">⚠️ <b>Fail-closed（不变）：</b>未知 / 无效 / SNA 档位默认禁止注入；AP 断开立即清零 Gate 计时。此策略由服务端 C++ 强制（handlers.h），客户端 UI 无法绕过。</div>
```

Replace it with the same line plus a new 2026.8.3.6 warning strip immediately after:

```
          <div class="safety-strip">⚠️ <b>Fail-closed（不变）：</b>未知 / 无效 / SNA 档位默认禁止注入；AP 断开立即清零 Gate 计时。此策略由服务端 C++ 强制（handlers.h），客户端 UI 无法绕过。</div>
          <div class="safety-strip"><b>⚠️ China 2026.8.3.6 风险：</b>该固件收紧预检，AP 激活边沿注入仍可能触发方向盘猛甩（即使开启 AP-First 仍有 &lt;5% 残留）。研究/教学用途，风险自担；强烈建议先 Listen-Only 验证。</div>
```

- [ ] **Step 2: Regenerate the minified UI**

Run: `python3 scripts/minify_dashboard.py`
Expected: completes without error; regenerates `include/web/mcp2515_dashboard_ui.h`.

- [ ] **Step 3: Verify the warning survived minification**

Run: `grep -c "2026.8.3.6" include/web/mcp2515_dashboard_ui.h`
Expected: `1` (or more) — the warning text is present in the embedded UI.

- [ ] **Step 4: Confirm no build-id stamp leaked into the commit**

Run: `git diff --stat include/web/mcp2515_dashboard_ui.h` then `git diff include/web/mcp2515_dashboard_ui.h | grep -c DASH_UI_BUILD_ID`
Expected: the diff exists (regenerated file), but the `DASH_UI_BUILD_ID` count is `0` (minify does not stamp; only a full `pio run` does). If a stamp is present, run `git restore include/web/mcp2515_dashboard_ui.h` and re-run Step 2.

- [ ] **Step 5: Commit**

```bash
git add include/web/mcp2515_dashboard_ui.src.h include/web/mcp2515_dashboard_ui.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" commit -m "ui(ap-card): warn CN 2026.8.3.6 residual steer-jerk risk + Listen-Only

Add a safety-strip under the AP-injection card noting the activation-edge
jerk risk remains <5% even with AP-First, per the steer-jerk diagnosis."
```

---

## Follow-up (out of this plan)

- **Edge-softening** the `0x3EE` enable bit (ramp instead of hard step) for the residual <5%. Needs a TX-echo capture to observe the injected frame first.
- **Audit other `isDASAutopilotActive` callers** beyond the AP-First gate (e.g., any FSD injection gate) for the same state-6 assumption.
- **Contribute the state-6 finding + diagnosis upstream** (`flipper-tesla-fsd#108`, `ev-open-can-tools#66`) — the reporter is on upstream firmware.
- **CI task #11** (skip pre-existing native bench failures) — separate, pending.
