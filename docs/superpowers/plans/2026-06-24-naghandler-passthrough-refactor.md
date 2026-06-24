# NagHandler Dual-Mode (Passthrough + Torque-Tamper) Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `NagHandler` into a dual-mode 0x370 nag-killer — default **passthrough** (torque untouched) and opt-in **torque-tamper** (1.80 Nm fixed) — sharing counter+1 / handsOn=1 / checksum, then bench-validate both modes on Jordan's real 1256-frame capture.

**Architecture:** Add one opt-in runtime global `nagTorqueTamperRuntime{false}` (mirroring the existing `nagKillerRuntime` pattern) and branch on it inside `NagHandler::handleMessage`. Remove the bionic sine-wave path + member. The dashboard exposes the opt-in via `/defense_config` + NVS + `/status`. A reframed guard contract asserts the tamper path stays opt-in and the 8 banned `DashEpasNag` symbols stay removed.

**Tech Stack:** ESP-IDF + PlatformIO; TWAI CAN; Unity native tests (`env:native_nag`); Python pytest guard; NVS preferences; ArduinoJson.

---

## Scope notes (deviations from spec §3/§5 — read before starting)

The approved spec (`docs/superpowers/specs/2026-06-24-naghandler-passthrough-refactor-design.md`) said "add `nagTorqueTamper` as a `Shared<bool>` member on `NagHandler`" and "a dashboard UI control." Investigation while writing this plan found two things that change that:

1. **`nagTorqueTamper` is a free global, not a NagHandler member.** The dashboard reaches handlers via `CarManagerBase *dashHandler` (`mcp2515_dashboard.h:112`) and a `handlerPool[3]` of `{Legacy, HW3, HW4}` (`:6672-6678`) — none is a `NagHandler*`, so a NagHandler-only member is unreachable from the dashboard. The existing NagHandler runtime knob `nagKillerRuntime` solves this by being an `inline Shared<bool>` global in `can_helpers.h:48` that NagHandler reads directly. We mirror that exactly with `nagTorqueTamperRuntime`. Coverage is identical: the guard asserts it defaults false and the tamper branch is gated on it; native tests set the global directly.

2. **Dashboard = backend `/defense_config` + NVS + `/status`; the minified web UI control is deferred.** The car-facing UI lives in a minified blob (`mcp2515_dashboard_ui.src.h`) that is fragile and error-prone to hand-edit. The backend toggle fully satisfies "executable for testing" (toggle via `curl -X POST /defense_config` or read via `/status`). The UI chip is a clean follow-up. This is flagged here so it can be objected to at plan review.

The 8 banned `DashEpasNag` symbols (`test/test_no_epas_nag_contract.py:FORBIDDEN`) stay banned. Only the *torque-tamper-within-NagHandler* policy changes (opt-in, default off).

## File Structure

| File | Responsibility | Action |
|---|---|---|
| `include/can_helpers.h` | NagHandler runtime knobs (free globals) | **modify** — add `nagTorqueTamperRuntime{false}` next to `nagKillerRuntime` |
| `include/handlers.h` | `NagHandler` (753-877) | **modify** — dual-mode branch, remove bionic path + member + overrides |
| `include/web/mcp2515_dashboard.h` | dashboard opt-in surface | **modify** — global + NVS + `/defense_config` + `/status`, mirror `bionicSteering` |
| `test/test_native_nag/test_nag_handler.cpp` | NagHandler unit + real-data tests | **modify** — rewrite 4 torque tests + T1 for dual mode; add opt-in/default-false regressions |
| `test/test_no_epas_nag_contract.py` | guard rail | **modify** — keep 8-symbol ban; add opt-in/default-false/gating assertions |
| `platformio.ini` | build profiles | **none** — `NAG_KILLER` stays off in waveshare production |

## Conventions (repo-specific, apply to every task)

- **Repo root:** `/Users/ziwind/my-vibe-project/waveshare-single-can-firmware`. Every shell command uses an absolute path or `cd` into it (the Bash tool resets cwd each call).
- **Git identity is unconfigured.** Every `git commit` MUST prepend `-c user.name=ziwind -c user.email=ziwind@Mac-mini.local`.
- **Run native NagHandler tests:** `pio test -e native_nag` (this is source of truth; ignore IDE/clang red squiggles on `unity.h`/`CanFrame`).
- **Run the guard:** `python3 -m pytest test/test_no_epas_nag_contract.py -q` (pytest-style, NOT unittest).
- **Real-data findings are swallowed by the PlatformIO native adapter** — surface them via `scripts/run_nag_bench.sh` (runs the compiled binary directly).
- **Do NOT edit `include/web/mcp2515_dashboard_ui*.h`** (minified; out of scope — see Scope note 2).

---

### Task 1: Add the `nagTorqueTamperRuntime` opt-in global

This task introduces the seam with zero behavior change — NagHandler still tampers unconditionally (it does not read the global yet). Suite stays green.

**Files:**
- Modify: `include/can_helpers.h:48` (next to `nagKillerRuntime`)

- [ ] **Step 1: Add the global**

In `include/can_helpers.h`, find the line (around line 48):

```cpp
inline Shared<bool> nagKillerRuntime{kNagKillerDefaultEnabled};
```

Add immediately AFTER it:

```cpp
// Opt-in torque-tamper mode for NagHandler 0x370 echo (1.80 Nm fixed torque).
// DEFAULT false = PASSTHROUGH (torque bytes untouched). True = TORQUE_TAMPER.
// Torque-tamper is the documented primary-suspect vector of the 2026-06-19 EPAS
// fault (docs/EPAS-NAG-REMOVAL-INCIDENT.md) — opt-in only, never the default.
inline Shared<bool> nagTorqueTamperRuntime{false};
```

- [ ] **Step 2: Verify the native suite still compiles + passes (behavior unchanged)**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_nag
```
Expected: all existing tests PASS (the global is declared but unread; no behavior change). If a test fails, it is pre-existing — stop and report.

- [ ] **Step 3: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && \
git add include/can_helpers.h && \
git -c user.name=ziwind -c user.email=ziwind@Mac-mini.local \
  commit -m "feat(nag): add nagTorqueTamperRuntime opt-in global (default false, unread)"
```

---

### Task 2: Dual-mode NagHandler + rewrite affected tests

This is the behavioral flip: default goes from torque-tamper → passthrough, with tamper retained behind the opt-in global. The 4 torque tests + T1 currently assert tamper-in-default and MUST be rewritten in the same commit or the suite goes red. Done TDD: tests first (red), then the handler change (green).

**Files:**
- Modify: `include/handlers.h:753-877` (full `NagHandler` struct)
- Modify: `test/test_native_nag/test_nag_handler.cpp` (helpers, 4 torque tests, T1, `setUp`, `main`)

- [ ] **Step 1: Rewrite the 4 torque tests + add 2 regressions (RED)**

Open `test/test_native_nag/test_nag_handler.cpp`.

(a) In `setUp()` (around line 59), reset the global so tests are isolated. Replace:

```cpp
void setUp()
{
    mock.reset();
    handler = NagHandler();
    handler.enablePrint = false;
}
```

with:

```cpp
void setUp()
{
    mock.reset();
    handler = NagHandler();
    handler.enablePrint = false;
    nagTorqueTamperRuntime = false; // default = PASSTHROUGH; tests opt in explicitly
}
```

(b) Replace the block of 4 torque tests (the functions `test_nag_sets_fixed_torque_0xB6`, `test_nag_torque_value_is_1_80_nm`, `test_nag_copies_bytes_0_1_2_5_unchanged`, and `test_nag_output_torque_never_exceeds_safe_range` — approximately lines 194-285) with these 5 functions:

```cpp
// ============================================================
// Torque mode: PASSTHROUGH (default) vs TORQUE_TAMPER (opt-in)
// ============================================================

// DEFAULT (passthrough): torque bytes pass through unchanged.
void test_nag_passthrough_default_leaves_torque_bytes_unchanged()
{
    nagTorqueTamperRuntime = false;
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C); // byte3 = low torque raw (!= 0xB6)
    uint8_t inByte2 = f.data[2];
    uint8_t inByte3 = f.data[3];
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(inByte2, mock.sent[0].data[2]);
    TEST_ASSERT_EQUAL_HEX8(inByte3, mock.sent[0].data[3]);
}

// OPT-IN (tamper): byte3 forced to 0xB6, byte2 low nibble 0x08 (1.80 Nm).
void test_nag_tamper_optin_sets_fixed_torque_0xB6()
{
    nagTorqueTamperRuntime = true;
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xB6, mock.sent[0].data[3]);
    TEST_ASSERT_EQUAL_HEX8(0x08, mock.sent[0].data[2] & 0x0F);
}

// OPT-IN (tamper): decoded torque == 1.80 Nm.
void test_nag_tamper_optin_torque_is_1_80_nm()
{
    nagTorqueTamperRuntime = true;
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    uint16_t tRaw = ((mock.sent[0].data[2] & 0x0F) << 8) | mock.sent[0].data[3];
    float torque = tRaw * 0.01f - 20.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1.80, torque);
}

// PASSTHROUGH: bytes 0,1,2,3,5 copied unchanged; tamper OFF by default.
void test_nag_passthrough_copies_bytes_0_1_2_3_5_unchanged()
{
    nagTorqueTamperRuntime = false;
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.data[0] = 0xAB;
    f.data[1] = 0xCD;
    f.data[2] = 0x8E; // upper nibble has flags
    f.data[3] = 0x77;
    f.data[5] = 0x42;
    // Recompute checksum after manual changes
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xAB, mock.sent[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, mock.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x8E, mock.sent[0].data[2]); // passthrough
    TEST_ASSERT_EQUAL_HEX8(0x77, mock.sent[0].data[3]); // passthrough
    TEST_ASSERT_EQUAL_HEX8(0x42, mock.sent[0].data[5]);
}

// Canary: in tamper mode, output torque stays at 1.80 Nm and within [-5, 5].
void test_nag_tamper_output_torque_never_exceeds_safe_range()
{
    nagTorqueTamperRuntime = true;
    for (uint8_t cnt = 0; cnt < 16; cnt++)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, -20.0 + cnt * 2.5, cnt);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());

        uint16_t tRaw = ((mock.sent[0].data[2] & 0x0F) << 8) | mock.sent[0].data[3];
        float torque = tRaw * 0.01f - 20.5f;

        // Must be exactly 1.80 Nm (from fixed byte 3 = 0xB6)
        TEST_ASSERT_FLOAT_WITHIN(0.1, 1.80, torque);
        // Must never exceed safe range
        TEST_ASSERT_TRUE(torque >= -5.0f);
        TEST_ASSERT_TRUE(torque <= 5.0f);
    }
}

// Safety regression: with NO opt-in, NagHandler must NOT inject 0xB6.
void test_nag_tamper_default_is_passthrough()
{
    // Do NOT set nagTorqueTamperRuntime here — setUp() left it false.
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C); // byte3 != 0xB6
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_NOT_EQUAL(0xB6, mock.sent[0].data[3]);
    TEST_ASSERT_EQUAL_HEX8(f.data[3], mock.sent[0].data[3]);
}
```

(c) Rewrite T1 (`test_nag_real_frames_echo_wellformed`, around lines 369-401) to assert per-mode and print a `[REAL-DATA]` line per mode (the bench surfaces these). Replace the whole function with:

```cpp
// T1: every echo NagHandler emits for a real frame must be well-formed, in BOTH
// modes. Passthrough: torque byte3 == input. Tamper: byte3 == 0xB6.
void test_nag_real_frames_echo_wellformed()
{
    const bool modes[2] = {false, true};
    const char *names[2] = {"passthrough", "tamper"};
    for (int m = 0; m < 2; m++)
    {
        nagTorqueTamperRuntime = modes[m];
        int echoed = 0;
        for (size_t i = 0; i < kRealEpasSampleCount; i++)
        {
            mock.reset();
            CanFrame f = makeFrameFromSample(kRealEpasSamples[i]);
            handler.handleMessage(f, mock);

            if (!kRealEpasSamples[i].expectEcho)
            {
                TEST_ASSERT_EQUAL(0, mock.sent.size());
                continue;
            }
            echoed++;
            const CanFrame &e = mock.sent[0];

            // checksum = sum(b0..b6) + 0x73
            uint16_t sum = 0;
            for (int j = 0; j < 7; j++)
                sum += e.data[j];
            TEST_ASSERT_EQUAL_HEX8((sum + 0x73) & 0xFF, e.data[7]);

            // counter = input + 1 (mod 16)
            uint8_t inCnt = f.data[6] & 0x0F;
            TEST_ASSERT_EQUAL_HEX8((inCnt + 1) & 0x0F, e.data[6] & 0x0F);

            // handsOnLevel = 1
            TEST_ASSERT_EQUAL_UINT8(1, (e.data[4] >> 6) & 0x03);

            if (modes[m])
            {
                TEST_ASSERT_EQUAL_HEX8(0xB6, e.data[3]);        // tamper
                TEST_ASSERT_EQUAL_HEX8(0x08, e.data[2] & 0x0F);
            }
            else
            {
                TEST_ASSERT_EQUAL_HEX8(f.data[3], e.data[3]);   // passthrough
            }
        }
        printf("[REAL-DATA] T1 mode=%s echoed=%d\n", names[m], echoed);
    }
    nagTorqueTamperRuntime = false; // restore default
}
```

(d) Update the `RUN_TEST` list in `main()` (around lines 529-543) to match. Replace the "// Modified fields" and "// Safety canary" blocks:

```cpp
    // Modified fields
    RUN_TEST(test_nag_sets_handson_to_1);
    RUN_TEST(test_nag_preserves_byte4_lower_bits);
    RUN_TEST(test_nag_passthrough_default_leaves_torque_bytes_unchanged);
    RUN_TEST(test_nag_passthrough_copies_bytes_0_1_2_3_5_unchanged);
    RUN_TEST(test_nag_tamper_optin_sets_fixed_torque_0xB6);
    RUN_TEST(test_nag_tamper_optin_torque_is_1_80_nm);
    RUN_TEST(test_nag_tamper_default_is_passthrough);

    // Checksum
    RUN_TEST(test_nag_checksum_correct);
    RUN_TEST(test_nag_checksum_correct_at_counter_boundary);
    RUN_TEST(test_nag_checksum_correct_with_various_inputs);

    // Safety canary
    RUN_TEST(test_nag_tamper_output_torque_never_exceeds_safe_range);
    RUN_TEST(test_nag_output_handson_never_exceeds_1);
```

- [ ] **Step 2: Run the suite to confirm it is RED**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_nag
```
Expected: FAIL. The new passthrough tests fail because the handler still unconditionally writes `0xB6` / `(frame.data[2] & 0xF0) | 0x08`. (Any compile error also counts as red — e.g. if the global name was mistyped.) Keep going.

- [ ] **Step 3: Refactor `NagHandler` in `include/handlers.h` (GREEN)**

Replace the entire `NagHandler` struct (lines 753-877 inclusive — from `struct NagHandler : public CarManagerBase` through the closing `};`) with:

```cpp
struct NagHandler : public CarManagerBase
{
    Shared<bool> nagKillerActive{true};
    Shared<uint32_t> nagEchoCount{0};

    const uint32_t *filterIds() const override
    {
        static constexpr uint32_t ids[] = {880, 920, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 3; }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (onFrame)
            onFrame(frame);
        updateHwDetectedFrom920(frame);
        if (frame.id != 880 || frame.dlc < 8)
            return;

        uint8_t handsOn = (frame.data[4] >> 6) & 0x03;

        if (!nagKillerActive || !nagKillerRuntime || handsOn != 0)
            return;
        if (checkAD && !checkAD())
            return;

        CanFrame echo;
        echo.id = 880;
        echo.dlc = 8;

        // Bytes copied through unchanged in BOTH modes.
        echo.data[0] = frame.data[0];
        echo.data[1] = frame.data[1];
        echo.data[5] = frame.data[5];

        // Torque mode select (opt-in global; default false = PASSTHROUGH).
        // TORQUE_TAMPER (1.80 Nm fixed) is the documented primary-suspect vector
        // of the 2026-06-19 EPAS fault (docs/EPAS-NAG-REMOVAL-INCIDENT.md) —
        // opt-in only, never the default code path.
        if (nagTorqueTamperRuntime)
        {
            echo.data[2] = (frame.data[2] & 0xF0) | 0x08; // sign nibble positive
            echo.data[3] = 0xB6;                           // 1.80 Nm fixed torque
        }
        else
        {
            echo.data[2] = frame.data[2];                  // PASSTHROUGH
            echo.data[3] = frame.data[3];                  // PASSTHROUGH
        }

        // handsOnLevel = 1 (gate above guarantees bits 7:6 == 0)
        echo.data[4] = frame.data[4] | 0x40;

        // Counter + 1 (low nibble)
        uint8_t cnt = (frame.data[6] & 0x0F);
        cnt = (cnt + 1) & 0x0F;
        echo.data[6] = (frame.data[6] & 0xF0) | cnt;

        // Checksum: sum(byte0..byte6) + 0x73
        uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] +
                       echo.data[3] + echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

        framesSent++;
        nagEchoCount++;
        driver.send(echo);

        if (enablePrint && (nagEchoCount % 500 == 1))
        {
            char buf[LogRingBuffer::kMaxMsgLen];
            snprintf(buf, sizeof(buf),
                     "NagHandler: echo=%u tamper=%s",
                     (unsigned int)(uint32_t)nagEchoCount,
                     (bool)nagTorqueTamperRuntime ? "ON" : "off");
            logRing.push(buf,
#ifndef NATIVE_BUILD
                         millis()
#else
                         0
#endif
            );
#ifndef NATIVE_BUILD
            Serial.println(buf);
#endif
        }
    }
};
```

What changed vs. the old block: the `DashBionicSteer bionic` member, the `bionicDisabled()` / `resetBionic()` overrides, the `useBionic` computation, the bionic sine-wave branch, and the bionic checksum-verify/fallback are all GONE. The torque bytes are now set by the single `if (nagTorqueTamperRuntime)` branch. The shared logic (copy bytes 0/1/5, handsOn=1, counter+1, checksum) is unchanged. The `bionicSteering` base member (`handlers.h:71`) and the base `bionicDisabled()`/`resetBionic()` virtuals (`:290-291`) are untouched — other handlers still use them; NagHandler simply inherits the base no-op defaults now.

- [ ] **Step 4: Run the suite to confirm GREEN**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_nag
```
Expected: all tests PASS, including the rewritten torque tests, the new regressions, and T1 (both modes). If anything fails, do NOT commit — re-read the failing assertion and the handler branch.

- [ ] **Step 5: Confirm the standalone bionic test still passes (we did not touch `DashBionicSteer`)**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_bionic_steer
```
Expected: PASS. (`DashBionicSteer` in `include/dash_bionic_steer.h:47` is unchanged; only NagHandler stopped using it.)

- [ ] **Step 6: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && \
git add include/handlers.h test/test_native_nag/test_nag_handler.cpp && \
git -c user.name=ziwind -c user.email=ziwind@Mac-mini.local \
  commit -m "refactor(nag): dual-mode NagHandler — passthrough default, torque-tamper opt-in; drop bionic path"
```

---

### Task 3: Reframe the guard contract

The guard (`test/test_no_epas_nag_contract.py`) keeps banning the 8 `DashEpasNag` symbols. It gains three NEW assertions that lock the new policy: the opt-in global defaults false, the tamper branch is gated on it, and the `0xB6` constant never appears in the default (passthrough) path.

**Files:**
- Modify: `test/test_no_epas_nag_contract.py` (add 3 test functions + `import re`)

- [ ] **Step 1: Add `import re` and three new tests**

Open `test/test_no_epas_nag_contract.py`. At the top, change:

```python
from pathlib import Path
```

to:

```python
import re
from pathlib import Path
```

Then, at the END of the file (after `test_no_native_epas_nag_env`), append:

```python
def test_nag_torque_tamper_global_defaults_false():
    """nagTorqueTamperRuntime must be declared with a false default (passthrough)."""
    ch = (ROOT / "include" / "can_helpers.h").read_text()
    assert "nagTorqueTamperRuntime" in ch, "nagTorqueTamperRuntime global must exist"
    assert re.search(r"nagTorqueTamperRuntime\s*\{[^}]*\bfalse\b", ch), (
        "nagTorqueTamperRuntime must be declared with a false default "
        "(passthrough is the default mode)"
    )


def test_nag_torque_tamper_is_gated_behind_opt_in():
    """The 0x370 torque-tamper (0xB6) must live inside the opt-in branch."""
    h = (ROOT / "include" / "handlers.h").read_text()
    assert "if (nagTorqueTamperRuntime)" in h, "tamper path must be gated on nagTorqueTamperRuntime"
    assert "0xB6" in h, "torque-tamper constant 0xB6 must be present in the opt-in branch"
    # The branch must carry the 2026-06-19 incident warning.
    assert "2026-06-19" in h and "EPAS" in h, (
        "torque-tamper branch must reference the 2026-06-19 EPAS incident warning"
    )


def test_nag_torque_tamper_constant_not_in_default_path():
    """The default (else) path must NOT write 0xB6 — it passes torque through."""
    h = (ROOT / "include" / "handlers.h").read_text()
    idx_if = h.find("if (nagTorqueTamperRuntime)")
    assert idx_if != -1, "opt-in branch missing"
    idx_else = h.find("else", idx_if)
    assert idx_else != -1, "else (passthrough) branch missing"
    tamper_block = h[idx_if:idx_else]
    passthrough_block = h[idx_else:idx_else + 400]
    assert "0xB6" in tamper_block, "0xB6 must be inside the opt-in branch"
    assert "0xB6" not in passthrough_block, (
        "0xB6 torque-tamper must NOT appear in the default (passthrough) path"
    )
```

- [ ] **Step 2: Run the guard**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && python3 -m pytest test/test_no_epas_nag_contract.py -q
```
Expected: 5 passed (the original 2 + the 3 new). The 8-symbol ban (`test_no_epas_nag_symbols_in_compiled_source`) must still pass — none of those symbols were re-introduced.

- [ ] **Step 3: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && \
git add test/test_no_epas_nag_contract.py && \
git -c user.name=ziwind -c user.email=ziwind@Mac-mini.local \
  commit -m "test(guard): assert NagHandler torque-tamper is opt-in/default-false (8-symbol ban kept)"
```

---

### Task 4: Dashboard opt-in surface (backend: `/defense_config` + NVS + `/status`)

Mirror the existing `bionicSteering` / `def_bio` wiring exactly, adding a `nagTorqueTamper` / `def_ntt` pair. This makes the opt-in toggle-able at runtime via HTTP and persistent across reboots, and visible in `/status`. (The minified web UI chip is deferred — see Scope note 2.)

**Files:**
- Modify: `include/web/mcp2515_dashboard.h` (9 edits, all anchored on existing `bionicSteering`/`def_bio` lines)

All edits use `Edit` with the exact `old_string` shown. Run `pio run -e waveshare_single_can_standalone` once at the end to confirm the production build still compiles.

- [ ] **Edit 1 — global declaration (around line 195)**

old_string:
```cpp
static bool dashBionicDisabled = false; // bionic auto-disabled after 3 failures
static bool dashApEapCompatible = true;
```
new_string:
```cpp
static bool dashBionicDisabled = false; // bionic auto-disabled after 3 failures
static bool dashNagTorqueTamper = false; // OPT-IN 0x370 torque-tamper (1.80Nm). DEFAULT OFF.
static bool dashApEapCompatible = true;
```

- [ ] **Edit 2 — NVS save (around line 1379)**

old_string:
```cpp
    prefs.putBool("def_bio", dashBionicSteering);
    prefs.putBool("def_nd", dashSpeedNoDisturb);
```
new_string:
```cpp
    prefs.putBool("def_bio", dashBionicSteering);
    prefs.putBool("def_ntt", dashNagTorqueTamper);
    prefs.putBool("def_nd", dashSpeedNoDisturb);
```

- [ ] **Edit 3 — NVS load + boot-sync the runtime global (around line 1582)**

old_string:
```cpp
    dashBionicSteering = prefs.getBool("def_bio", false);
    dashSpeedNoDisturb = prefs.getBool("def_nd", false);
```
new_string:
```cpp
    dashBionicSteering = prefs.getBool("def_bio", false);
    dashNagTorqueTamper = prefs.getBool("def_ntt", false);
    nagTorqueTamperRuntime = dashNagTorqueTamper; // boot-sync opt-in to NagHandler
    dashSpeedNoDisturb = prefs.getBool("def_nd", false);
```

- [ ] **Edit 4 — `handleDefenseConfig` hasArg condition (around line 3115)**

old_string:
```cpp
    if (server.hasArg("enabled") || server.hasArg("bionic_steering") ||
        server.hasArg("sound_warning_suppression") || server.hasArg("speed_no_disturb") ||
        server.hasArg("ap_eap_compatible") || server.hasArg("dnd_volume") ||
        server.hasArg("dnd_speed") || server.hasArg("isa_override"))
```
new_string:
```cpp
    if (server.hasArg("enabled") || server.hasArg("bionic_steering") ||
        server.hasArg("sound_warning_suppression") || server.hasArg("speed_no_disturb") ||
        server.hasArg("ap_eap_compatible") || server.hasArg("dnd_volume") ||
        server.hasArg("dnd_speed") || server.hasArg("isa_override") ||
        server.hasArg("nag_torque_tamper"))
```

- [ ] **Edit 5 — `handleDefenseConfig` handler block (insert after the bionic_steering block, around line 3139)**

old_string:
```cpp
            if (dashHandler)
            {
                dashHandler->bionicSteering = v;
                if (v)
                    dashHandler->resetBionic((uint32_t)millis());
            }
        }
        if (server.hasArg("sound_warning_suppression"))
```
new_string:
```cpp
            if (dashHandler)
            {
                dashHandler->bionicSteering = v;
                if (v)
                    dashHandler->resetBionic((uint32_t)millis());
            }
        }
        if (server.hasArg("nag_torque_tamper"))
        {
            // WARNING: torque-tamper is the documented primary-suspect vector of
            // the 2026-06-19 EPAS fault. Opt-in only; never the default.
            bool v = dashArgTruthy(server.arg("nag_torque_tamper"));
            dashNagTorqueTamper = v;
            nagTorqueTamperRuntime = v; // sync to NagHandler immediately
        }
        if (server.hasArg("sound_warning_suppression"))
```

- [ ] **Edit 6 — status JSON stored var (around line 5532)**

old_string:
```cpp
    bool storedBionicSteering = dashBionicSteering;
    bool storedSpeedNoDisturb = dashSpeedNoDisturb;
```
new_string:
```cpp
    bool storedBionicSteering = dashBionicSteering;
    bool storedNagTorqueTamper = dashNagTorqueTamper;
    bool storedSpeedNoDisturb = dashSpeedNoDisturb;
```

- [ ] **Edit 7 — status JSON NVS-backed load (around line 5593)**

old_string:
```cpp
        storedBionicSteering = p.getBool("def_bio", dashBionicSteering);
        storedSpeedNoDisturb = p.getBool("def_nd", dashSpeedNoDisturb);
```
new_string:
```cpp
        storedBionicSteering = p.getBool("def_bio", dashBionicSteering);
        storedNagTorqueTamper = p.getBool("def_ntt", dashNagTorqueTamper);
        storedSpeedNoDisturb = p.getBool("def_nd", dashSpeedNoDisturb);
```

- [ ] **Edit 8 — status JSON emit (around line 5728)**

old_string:
```cpp
    j += ",\"bionicSteering\":" + String(storedBionicSteering ? "true" : "false");
    j += ",\"speedNoDisturb\":" + String(storedSpeedNoDisturb ? "true" : "false");
```
new_string:
```cpp
    j += ",\"bionicSteering\":" + String(storedBionicSteering ? "true" : "false");
    j += ",\"nagTorqueTamper\":" + String(storedNagTorqueTamper ? "true" : "false");
    j += ",\"speedNoDisturb\":" + String(storedSpeedNoDisturb ? "true" : "false");
```

- [ ] **Edit 9 — POST JSON save (around line 6007)**

old_string:
```cpp
        if (defense["bionicSteering"].is<bool>())
            p.putBool("def_bio", defense["bionicSteering"].as<bool>());
        if (defense["speedNoDisturb"].is<bool>())
            p.putBool("def_nd", defense["speedNoDisturb"].as<bool>());
```
new_string:
```cpp
        if (defense["bionicSteering"].is<bool>())
            p.putBool("def_bio", defense["bionicSteering"].as<bool>());
        if (defense["nagTorqueTamper"].is<bool>())
            p.putBool("def_ntt", defense["nagTorqueTamper"].as<bool>());
        if (defense["speedNoDisturb"].is<bool>())
            p.putBool("def_nd", defense["speedNoDisturb"].as<bool>());
```

- [ ] **Step: Verify the production build compiles**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone
```
Expected: `SUCCESS`. (This env compiles the dashboard; a missed/wrong edit fails here. `NAG_KILLER` stays OFF in this env, so production behavior is unchanged — NagHandler is built but the runtime gate `nagKillerRuntime` keeps it inert unless defense is enabled.)

- [ ] **Step: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && \
git add include/web/mcp2515_dashboard.h && \
git -c user.name=ziwind -c user.email=ziwind@Mac-mini.local \
  commit -m "feat(dashboard): expose NagHandler torque-tamper opt-in (def_ntt NVS + /defense_config + /status)"
```

---

### Task 5: Full validation + bench

No code changes. Run every gate end-to-end and surface the real-data findings for both modes.

**Files:** none

- [ ] **Step 1: Native NagHandler suite (both modes)**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_nag
```
Expected: all PASS (the rewritten torque tests, the opt-in/default-false regressions, and T1 across both modes).

- [ ] **Step 2: Real-data bench findings (both modes)**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && bash scripts/run_nag_bench.sh
```
Expected: prints `[REAL-DATA]` lines, including (from the rewritten T1):
```
[REAL-DATA] T1 mode=passthrough echoed=<N>
[REAL-DATA] T1 mode=tamper echoed=<N>
```
(plus the existing T3 interleave and T4 handsOn findings). `echoed` must be equal across modes (same frames pass the handsOn==0 gate; only the torque bytes differ). Collisions must remain 0 for the clean +2 stride.

- [ ] **Step 3: Guard contract**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && python3 -m pytest test/test_no_epas_nag_contract.py -q
```
Expected: 5 passed.

- [ ] **Step 4: Bionic standalone (regression — we removed NagHandler's use of it)**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio test -e native_bionic_steer
```
Expected: PASS.

- [ ] **Step 5: Production build (dashboard wiring compiles)**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware && pio run -e waveshare_single_can_standalone
```
Expected: `SUCCESS`.

- [ ] **Step 6: Report findings (no commit)**

Summarize for Jordan: both modes produce well-formed echoes on all 1246 handsOn==0 real frames; counter collisions stay 0; tamper is opt-in/default-off; the 8 `DashEpasNag` symbols remain banned; production build is unchanged (NAG_KILLER off). State plainly that the bench verifies frame well-formedness ONLY — it does NOT verify on-vehicle safety for either mode (the 2026-06-19 fault involved torque-tamper; passthrough is unverified on Jordan's vehicle; on-car test is gated by separate sign-off).

---

## Self-review

- **Spec coverage:** §3 dual-mode branch → Task 2. §3 bionic removal → Task 2 (member + overrides + branch + verify all removed; `DashBionicSteer` struct kept, Task 5 Step 4 confirms). §4 default-off → Task 1 (global `{false}`) + Task 3 (guard). §4 explicit opt-in → Task 4 (dashboard). §4 guard reframe → Task 3. §5 handlers.h → Task 2; mcp2515_dashboard.h → Task 4; test_nag_handler.cpp → Task 2; test_no_epas_nag_contract.py → Task 3; platformio.ini none. §6 unit rewrite → Task 2; new regression → Task 2; T1–T4 → Task 2 (T1) + Task 5 (T2 unchanged, T3/T4 unchanged, re-surfaced). §7 invariants pinned in the Task 2 source comment. All sections covered.
- **Placeholder scan:** no TBD/TODO; every code step shows full code; every command shows expected output.
- **Type/name consistency:** the global is `nagTorqueTamperRuntime` everywhere (can_helpers.h, handlers.h branch + print, dashboard Edit 3/5, test setUp, guard regex). NVS key `def_ntt`, dashboard global `dashNagTorqueTamper`, JSON field `nagTorqueTamper`, HTTP arg `nag_torque_tamper` — consistent within their layers and mirror the `bionicSteering`/`def_bio` naming.
- **Refinement flags:** two deviations from spec are stated up front (global-not-member; UI deferred). Flag at plan review.
