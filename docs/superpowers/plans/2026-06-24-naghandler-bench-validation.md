# NagHandler Real-Data Bench Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Feed Jordan's real captured 0x370 EPAS frames through the existing legacy `NagHandler` and produce evidence: (a) the echo is well-formed on real data, (b) whether the +1 echo collides with the observed real counter stride.

**Architecture:** A Python codegen script decodes the capture txt into a C++ data header (`real_epas_frames.h`). Four new native tests in `test/test_native_nag/test_nag_handler.cpp` replay those frames through `NagHandler` (via the existing `MockDriver`) and assert well-formedness + report counter-collision / handsOn-signal findings. No production code changes; `NagHandler` stays compile-gated by `NAG_KILLER` (off in waveshare production).

**Tech Stack:** PlatformIO native env (`[env:native_nag]`, `-DNAG_KILLER`), Unity test framework, C++17, Python 3 (codegen + its unittest).

**Spec:** `docs/superpowers/specs/2026-06-24-naghandler-bench-validation-design.md`

**Key APIs (verified):**
- `CanFrame { uint32_t id; uint8_t dlc; uint8_t data[8]; uint8_t bus; }` (`include/can_frame_types.h:18-24`)
- `MockDriver`: `std::vector<CanFrame> sent;` + `void reset()` + `bool send(const CanFrame&)` (`include/drivers/mock_driver.h:12-36`)
- `NagHandler::handleMessage(CanFrame&, CanDriver&)` — echoes 0x370 (id 880) with counter+1, handsOn=1, byte3=0xB6, checksum = sum(b0..6)+0x73, **only when** `(data[4]>>6)&0x03 == 0` (`include/handlers.h:773-877`)
- The global `handler` and `mock` already exist in `test_nag_handler.cpp:8-9`; `setUp()` (line 45) resets both per test.

**Correction to spec §9 step 2:** `[env:native_single_can_dashboard]` has `test_filter = test_native_dashboard` (`platformio.ini:71`) and does **not** compile `test_native_nag/`. The single-CAN compile check is therefore not meaningful via that env; `NagHandler` compilation is fully covered by `[env:native_nag]`. This plan uses `pio test -e native_nag` only.

---

## File Structure

| File | Responsibility | Action |
|---|---|---|
| `scripts/decode_epas_capture.py` | Parse Jordan's capture txt → emit `real_epas_frames.h`. Pure functions + CLI. Importable for its own unittest. | **create** |
| `test/test_decode_epas_capture.py` | Unittest for the parser (line→bytes, expectEcho, header shape). | **create** |
| `test/test_native_nag/real_epas_frames.h` | Auto-generated array of real frames (raw bytes + expectEcho + tag) + count. | **create (generated)** |
| `test/test_native_nag/test_nag_handler.cpp` | Append `#include "real_epas_frames.h"`, a `makeFrameFromSample` helper, and 4 new tests (T1–T4) + their `RUN_TEST` lines. | **modify** |

No other files change. `platformio.ini`, `include/handlers.h`, `include/app.h`, and the waveshare production profile are untouched.

---

## Task 1: Decode script (TDD — parser unittest first)

**Files:**
- Create: `test/test_decode_epas_capture.py`
- Create: `scripts/decode_epas_capture.py`

- [ ] **Step 1: Write the failing parser unittest**

Create `test/test_decode_epas_capture.py`:

```python
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
import decode_epas_capture as dec


class TestDecodeEpasCapture(unittest.TestCase):
    def test_parses_one_hex_line(self):
        line = ("\U0001f4aa [CAN A - 0x370] 方向盘实际受力: +0.13 Nm "
                "\t(HEX: 52 08 08 0F 20 2C 25 55 )")
        frames = dec.parse_frames(line)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0][0], [0x52, 0x08, 0x08, 0x0F, 0x20, 0x2C, 0x25, 0x55])

    def test_expect_echo_true_when_handson_bits_zero(self):
        # byte4 = 0x20 -> bits 7:6 = 00 -> expectEcho True
        line = "(HEX: 52 08 08 0F 20 2C 25 55 )"
        self.assertTrue(dec.parse_frames(line)[0][1])

    def test_expect_echo_false_when_handson_bits_nonzero(self):
        # byte4 = 0xC0 -> bits 7:6 = 11 -> expectEcho False
        line = "(HEX: 52 08 08 0F C0 2C 25 99 )"
        self.assertFalse(dec.parse_frames(line)[0][1])

    def test_ignores_non_hex_lines(self):
        self.assertEqual(dec.parse_frames("noise\n<<< dismissed\n"), [])

    def test_tag_increments_on_warning_marker(self):
        text = ("\U0001f6a8❗ [monitor] 检测到握盘警告。\n"
                "(HEX: 52 08 08 0F 20 2C 25 55 )\n")
        frames = dec.parse_frames(text)
        self.assertEqual(frames[0][2], "e1")

    def test_format_header_has_array_and_a_byte(self):
        h = dec.format_header([([0x52, 0x08, 0x08, 0x0F, 0x20, 0x2C, 0x25, 0x55], True, "e1")])
        self.assertIn("kRealEpasSamples[]", h)
        self.assertIn("0x52", h)
        self.assertIn("true", h)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest discover -s test -p 'test_decode_epas_capture.py' -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'decode_epas_capture'`.

- [ ] **Step 3: Implement the decode script**

Create `scripts/decode_epas_capture.py`:

```python
#!/usr/bin/env python3
"""Decode real 0x370 EPAS frames from Jordan's capture into a C++ test header.

This is a data-prep codegen step (not visualization). It reads the capture txt,
extracts each frame's 8 data bytes in order, and emits a C++ header consumed by
test/test_native_nag/test_nag_handler.cpp.

Usage:
    python3 scripts/decode_epas_capture.py <capture.txt> -o test/test_native_nag/real_epas_frames.h
"""
import argparse
import re
import sys

# Matches "(HEX: 52 08 08 0F 20 2C 25 55 )" — 8 space-separated hex bytes.
_FRAME_RE = re.compile(
    r"HEX:\s*"
    r"([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+"
    r"([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})"
)

# Lines that mark a new "hold the wheel" warning event (increments the tag).
_WARNING_MARKERS = ("握盘警告", "握方向盘")  # 握盘警告 / 握方向盘


def _expect_echo(b):
    """NagHandler echoes only when handsOn (byte4 bits 7:6) == 0."""
    return ((b[4] >> 6) & 0x03) == 0


def parse_frames(text):
    """Return a list of (bytes_list[8], expectEcho_bool, tag_str), in capture order."""
    out = []
    event = 0
    saw_warning = False
    for line in text.splitlines():
        if any(marker in line for marker in _WARNING_MARKERS) and "警告" in line:
            event += 1
            saw_warning = True
        m = _FRAME_RE.search(line)
        if m:
            b = [int(x, 16) for x in m.groups()]
            tag = ("e%d" % event) if saw_warning else "pre"
            out.append((b, _expect_echo(b), tag))
    return out


_HEADER_PRELUDE = """\
#pragma once
// AUTO-GENERATED by scripts/decode_epas_capture.py --- DO NOT HAND-EDIT.
// Source: Jordan's real 0x370 EPAS capture (steering-wheel nag event).
// Regenerate:
//   python3 scripts/decode_epas_capture.py <capture.txt> -o test/test_native_nag/real_epas_frames.h
#include <cstdint>

struct RealEpasSample {
    uint8_t bytes[8];
    bool expectEcho;
    const char* tag;
};

static const RealEpasSample kRealEpasSamples[] = {
"""

_HEADER_POSTLUDE = """\
};

static const size_t kRealEpasSampleCount = sizeof(kRealEpasSamples) / sizeof(kRealEpasSamples[0]);
"""


def format_header(samples):
    """Render samples (list of (bytes, expectEcho, tag)) as the C++ header text."""
    lines = [_HEADER_PRELUDE]
    for b, eo, tag in samples:
        bs = ", ".join("0x%02X" % x for x in b)
        lines.append("    {{%s}, %s, \"%s\"}," % (bs, "true" if eo else "false", tag))
    lines.append(_HEADER_POSTLUDE)
    return "".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Decode 0x370 capture -> C++ header.")
    ap.add_argument("capture", help="path to the capture .txt")
    ap.add_argument("-o", "--output", default=None, help="output header path (default: stdout)")
    args = ap.parse_args()

    with open(args.capture, encoding="utf-8") as fh:
        text = fh.read()
    samples = parse_frames(text)
    header = format_header(samples)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(header)
        sys.stderr.write("wrote %d frames to %s\n" % (len(samples), args.output))
    else:
        sys.stdout.write(header)


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m unittest discover -s test -p 'test_decode_epas_capture.py' -v`
Expected: PASS — 6 tests OK.

- [ ] **Step 5: Commit**

```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
git add scripts/decode_epas_capture.py test/test_decode_epas_capture.py
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "test(nag): add capture decoder + parser unittest for real-data bench"
```

---

## Task 2: Generate the real-frames header

**Files:**
- Create (generated): `test/test_native_nag/real_epas_frames.h`

- [ ] **Step 1: Run the decoder against Jordan's capture**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
python3 scripts/decode_epas_capture.py \
  "/Users/ziwind/Codex/DouyinFSD /抓包握方向盘警告后扭矩数据 2.txt" \
  -o test/test_native_nag/real_epas_frames.h
```
Expected (on stderr): `wrote <N> frames to test/test_native_nag/real_epas_frames.h` where `<N>` is a few hundred (the capture holds several hundred 0x370 lines).

- [ ] **Step 2: Verify the header shape and a known frame**

Run:
```bash
head -12 test/test_native_nag/real_epas_frames.h
grep -c "0x52, 0x08" test/test_native_nag/real_epas_frames.h
tail -3 test/test_native_nag/real_epas_frames.h
```
Expected:
- First data row begins `    {{0x52, 0x08, 0x08, 0x0F, 0x20, 0x2C, 0x25, 0x55}, true, "pre"},` (matches capture line 8).
- `grep -c` returns ≥ 1.
- Tail shows the `kRealEpasSampleCount` definition.

- [ ] **Step 3: Commit the generated header**

```bash
git add test/test_native_nag/real_epas_frames.h
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "test(nag): add real captured 0x370 frames fixture (auto-generated)"
```

---

## Task 3: T1 + T2 — echo well-formedness and echo-count on real data

**Files:**
- Modify: `test/test_native_nag/test_nag_handler.cpp` (add include + helper after line 6; add 2 tests; add 2 `RUN_TEST` lines in `main()`)

- [ ] **Step 1: Add the include and the frame-builder helper**

In `test/test_native_nag/test_nag_handler.cpp`, after the existing `#include "drivers/mock_driver.h"` (line 6), add:

```cpp
#include <cstring>
#include "real_epas_frames.h"

// Build a CanFrame (id 880 / dlc 8) from a raw-bytes sample. Stored as raw
// bytes in the fixture to keep the generator portable (no designated-init of
// the data[] array in the header).
static CanFrame makeFrameFromSample(const RealEpasSample &s)
{
    CanFrame f;
    f.id = 880;
    f.dlc = 8;
    std::memcpy(f.data, s.bytes, 8);
    return f;
}
```

- [ ] **Step 2: Append the two new tests** (place them just before `int main()`, i.e. before line 350):

```cpp
// ============================================================
// Real-data bench validation (Jordan's captured 0x370 frames)
// ============================================================

// T1: every echo NagHandler emits for a real frame must be well-formed.
void test_nag_real_frames_echo_wellformed(void)
{
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
        TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
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

        // legacy fixed torque 0xB6 (bionic off by default in tests)
        TEST_ASSERT_EQUAL_HEX8(0xB6, e.data[3]);
    }
}

// T2: total echo count over the real sequence == number of handsOn==0 frames.
void test_nag_real_echo_count_matches_handson0(void)
{
    size_t expected = 0;
    for (size_t i = 0; i < kRealEpasSampleCount; i++)
        if (kRealEpasSamples[i].expectEcho)
            expected++;

    for (size_t i = 0; i < kRealEpasSampleCount; i++)
    {
        CanFrame f = makeFrameFromSample(kRealEpasSamples[i]);
        handler.handleMessage(f, mock);
    }
    TEST_ASSERT_EQUAL_UINT32(expected, mock.sent.size());
}
```

- [ ] **Step 3: Register the two tests** in `main()`. Add these two lines immediately before `return UNITY_END();` (after the existing last `RUN_TEST(test_nag_output_dlc_is_8);`):

```cpp
    // Real-data bench validation
    RUN_TEST(test_nag_real_frames_echo_wellformed);
    RUN_TEST(test_nag_real_echo_count_matches_handson0);
```

- [ ] **Step 4: Run the suite and verify**

Run: `pio test -e native_nag`
Expected: 28 tests pass (26 existing + 2 new). No failures.

- [ ] **Step 5: Commit**

```bash
git add test/test_native_nag/test_nag_handler.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "test(nag): replay real 0x370 frames, assert echo well-formedness + count"
```

---

## Task 4: T3 — counter-interleaving collision analysis (core)

**Files:**
- Modify: `test/test_native_nag/test_nag_handler.cpp`

- [ ] **Step 1: Append the collision-analysis test** (before `int main()`):

```cpp
// T3: counter-interleaving collision analysis (core).
// Simulate the bus timeline: real frames arrive in capture order; NagHandler
// emits an echo (counter = real+1) after each handsOn==0 frame. A "collision"
// is when a real frame's counter equals the immediately-preceding echo's
// counter -- i.e. EPAS would see a duplicate/stale counter (the 2026-06-19
// fault mechanism). We read the ACTUAL echo counter from NagHandler (not an
// assumption) to stay faithful to the implementation.
//
// NOTE: across warning-event boundaries the capture pauses, so the real
// counter sequence may show non-+2 deltas there. The distribution is printed
// so the finding is visible regardless of where gaps fall.
void test_nag_real_counter_interleave_analysis(void)
{
    int collisions = 0;
    int stridesPlus2 = 0;
    int stridesOther = 0;
    int prevRealCounter = -1;
    int prevEchoCounter = -1;

    for (size_t i = 0; i < kRealEpasSampleCount; i++)
    {
        CanFrame f = makeFrameFromSample(kRealEpasSamples[i]);
        uint8_t realCnt = f.data[6] & 0x0F;

        if (prevRealCounter >= 0)
        {
            int delta = (realCnt - prevRealCounter) & 0x0F;
            if (delta == 2)
                stridesPlus2++;
            else
                stridesOther++;
            // collision: this real frame duplicates the previous echo's counter
            if (prevEchoCounter >= 0 && realCnt == prevEchoCounter)
                collisions++;
        }
        prevRealCounter = realCnt;

        // drive NagHandler to obtain the actual echo counter
        int echoCounter = -1;
        if (kRealEpasSamples[i].expectEcho)
        {
            mock.reset();
            handler.handleMessage(f, mock);
            if (mock.sent.size() == 1)
                echoCounter = mock.sent[0].data[6] & 0x0F;
        }
        prevEchoCounter = echoCounter;
    }

    printf("[REAL-DATA] interleave: frames=%u strides(+2)=%d strides(other)=%d collisions=%d\n",
           (unsigned)kRealEpasSampleCount, stridesPlus2, stridesOther, collisions);

    // The analysis must have run over a real sequence.
    TEST_ASSERT_TRUE(stridesPlus2 + stridesOther > 0);
    // If the real sequence strides UNIFORMLY +2, echo(+1) cannot collide with
    // the next real frame (N -> N+2, echo is N+1). Assert the clean case.
    // If there are boundary gaps (stridesOther > 0), only print -- don't over-claim.
    if (stridesOther == 0 && stridesPlus2 > 0)
        TEST_ASSERT_EQUAL_INT(0, collisions);
}
```

- [ ] **Step 2: Register the test** in `main()` (add to the real-data block from Task 3):

```cpp
    RUN_TEST(test_nag_real_counter_interleave_analysis);
```

- [ ] **Step 3: Run the suite and read the finding**

Run: `pio test -e native_nag`
Expected: 29 tests pass. In the output, grep for the finding:
```bash
pio test -e native_nag 2>&1 | grep "\[REAL-DATA\] interleave"
```
Interpret the printed line:
- `strides(+2)=K strides(other)=0 collisions=0` → the real sequence strides uniformly +2 and the +1 echo does **not** collide (clean case, assertion passed). This is evidence the *counter-conflict* vector would not arise from NagHandler on this stride — but it does **not** prove on-vehicle safety (the torque/handsOn semantic-contradiction vector remains untested).
- `strides(other)>0` → capture has gaps (likely at event boundaries); the numeric assertion is skipped, only the distribution is reported.

- [ ] **Step 4: Commit**

```bash
git add test/test_native_nag/test_nag_handler.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "test(nag): counter-interleave collision analysis on real frames"
```

---

## Task 5: T4 — handsOn signal sanity

**Files:**
- Modify: `test/test_native_nag/test_nag_handler.cpp`

- [ ] **Step 1: Append the handsOn-sanity test** (before `int main()`):

```cpp
// T4: handsOn signal sanity.
// NagHandler keys off 0x370 byte4 bits[7:6]. If these never go non-zero in the
// real capture -- even during the driver's manual dismissal -- then NagHandler's
// trigger is inert on this vehicle and cannot detect "hands returned". The
// authoritative nag level is 0x399 byte5 (per the public-source reference).
// This test SURFACES the finding; it does not hide it.
void test_nag_real_handson_signal_sanity(void)
{
    int handson0 = 0;
    int handsonNonzero = 0;
    for (size_t i = 0; i < kRealEpasSampleCount; i++)
    {
        uint8_t ho = (kRealEpasSamples[i].bytes[4] >> 6) & 0x03;
        if (ho == 0)
            handson0++;
        else
            handsonNonzero++;
    }
    printf("[REAL-DATA] handsOn: total=%u ==0=%d !=0=%d\n",
           (unsigned)kRealEpasSampleCount, handson0, handsonNonzero);

    // Sanity: every frame was classified into one bucket.
    TEST_ASSERT_EQUAL_INT((int)kRealEpasSampleCount, handson0 + handsonNonzero);
    // Expected finding (not asserted): handsonNonzero == 0. Logged above for review.
}
```

- [ ] **Step 2: Register the test** in `main()`:

```cpp
    RUN_TEST(test_nag_real_handson_signal_sanity);
```

- [ ] **Step 3: Run the suite and read the finding**

Run: `pio test -e native_nag`
Expected: 30 tests pass (26 existing + 4 new).
```bash
pio test -e native_nag 2>&1 | grep "\[REAL-DATA\] handsOn"
```
Interpret: expected `!=0=0` (byte4 bits 7:6 stuck at 0). If so, NagHandler's trigger never toggles on this vehicle — feed this into the deferred decision (keep NagHandler vs pivot to 0x399-driven 0x3C2 DND).

- [ ] **Step 4: Commit**

```bash
git add test/test_native_nag/test_nag_handler.cpp
git -c user.name="ziwind" -c user.email="ziwind@Mac-mini.local" \
  commit -m "test(nag): handsOn signal sanity on real frames (0x370 byte4 trigger)"
```

---

## Task 6: Full validation + EPAS-ban guard regression

**Files:** none modified (verification only).

- [ ] **Step 1: Run the full native_nag suite once more**

Run: `pio test -e native_nag`
Expected: 30 tests pass. Capture both `[REAL-DATA]` lines for the record:
```bash
pio test -e native_nag 2>&1 | grep "\[REAL-DATA\]"
```

- [ ] **Step 2: Confirm the EPAS-nag ban guard is still green**

Run:
```bash
cd /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
python3 -m unittest discover -s test -p 'test_no_epas_nag_contract.py' -v
```
Expected: PASS. `NagHandler` is explicitly exempted by the guard (`test_no_epas_nag_contract.py:16`); this step verifies no regression. The 8 banned symbols (`DashEpasNag`, `dashEpasNag`, `epasNagDiag`, `tryBuildEcho`, `nextDemandTorque`, `EpasNagTraceRing`, `dash_epas_nag`, `def_epnag`) remain at 0 hits.

- [ ] **Step 3: Confirm no production surface changed**

Run:
```bash
git diff --stat main HEAD -- include/ src/ platformio.ini
grep -c "NAG_KILLER" platformio.ini   # confirm waveshare profile still has NO -DNAG_KILLER
```
Expected: `git diff --stat` over `include/ src/ platformio.ini` shows **no** changes attributable to this work (only prior commits). The `grep -c` over the waveshare profile section: NAG_KILLER must appear only in native envs (lines 63/69/88), **not** in `[env:waveshare_single_can_standalone]` (lines 11–41).

- [ ] **Step 4: Record findings + final commit (if any scratch notes)**

If you keep a findings note, add it under `docs/` (optional). Otherwise the two `[REAL-DATA]` lines captured in Step 1 are the deliverable. No further commit needed unless notes were added.

---

## Self-Review (run after writing the plan)

**1. Spec coverage** — every spec section maps to a task:
- §3 goal (well-formedness + collision evidence) → T1/T2 (Task 3) + T3 (Task 4).
- §3 honest scope limit → Safety notes in Task 4/6 + plan header.
- §4 data source (decode real frames, preserve order) → Tasks 1–2.
- §5 T1–T4 → Tasks 3 (T1+T2), 4 (T3), 5 (T4).
- §6 file changes → all four files covered (Task 1 ×2, Task 2 ×1, Tasks 3–5 ×1 modify).
- §7 F1/F2 expected findings → Task 4 (F1) + Task 5 (F2) PRINTF lines + interpretation.
- §8 safety invariants → Task 6 step 2 (guard) + step 3 (production untouched) + plan header.
- §9 validation → Task 6 (corrected: `native_nag` only, not `native_single_can_dashboard`).
- §10 deferred decisions → out of scope; flagged in Task 4/5 interpretation notes.

**2. Placeholder scan** — none. Every code step shows complete code; every command shows expected output. No "TBD"/"add error handling"/"similar to Task N".

**3. Type consistency** — `RealEpasSample { uint8_t bytes[8]; bool expectEcho; const char* tag; }`, `kRealEpasSamples[]`, `kRealEpasSampleCount`, and `makeFrameFromSample(const RealEpasSample&)` are used identically across Tasks 2–5. Python side: `parse_frames` returns `(bytes, expectEcho, tag)` 3-tuples consumed identically by `format_header` and the unittest. `MockDriver::sent` / `reset()` / `handler.handleMessage` match the verified APIs.
