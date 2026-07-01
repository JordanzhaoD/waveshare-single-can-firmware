# Legacy Speed & Safety v2 Design

Date: 2026-07-01
Project: `waveshare-single-can-firmware`
Status: Approved design, pending implementation plan

## 1. Purpose

Legacy Speed & Safety v2 improves the current Waveshare single-CAN Legacy driving experience without expanding into unverified CAN write paths.

The design has two goals:

1. Add a smarter Legacy speed offset strategy inspired by DouyinFSD's speed-target algorithm, but keep the final write path limited to the already-used Legacy `0x2F8 / 760 UI_gpsVehicleSpeed` offset field.
2. Add an experimental Abort Guard, matching flipper-tesla-fsd v2.16-beta.11's default-off probe behavior, to stop further injection after the vehicle reports AP abort states.

This is not a NAG suppression expansion and does not add new `0x370` behavior.

## 2. References Reviewed

### Local project

- `include/dash_legacy_speed.h`
- `include/handlers.h`
- `include/can_helpers.h`
- `include/web/mcp2515_dashboard.h`
- `docs/steer-jerk-diagnosis-20260623.md`

Current local behavior already includes:

- Legacy `0x2F8 / 760` `UI_userSpeedOffset` writes.
- Legacy MPP speed-limit override support.
- `0x438` vision speed slider support.
- `0x399` AP state and fused speed-limit reads.
- AP-First and Soft Engage for Legacy `0x3EE` activation.
- AP state 6 recognition in `isDASAutopilotActive()`.

### SLX upstream

`https://gitlab.com/slxslx/tesla-open-can-mod-slx-repo`

The SLX Legacy path does not provide a direct Legacy speed-offset implementation. It mainly handles:

- `0x045 / 69` stalk position to speed profile.
- `0x3EE / 1006` mux0 FSD activation and profile injection.
- `0x3EE / 1006` mux1 NAG bit handling.

SLX speed offset logic primarily targets HW3/HW4 `0x3FD / 1021`, so it must not be copied into this Legacy design as-is.

### flipper-tesla-fsd v2.16-beta.11

`https://github.com/hypery11/flipper-tesla-fsd/releases/tag/v2.16-beta.11`

Abort Guard is experimental and off by default. It listens for DAS AP states `8` and `9`, latches injection blocking, and clears only after a clean disengage.

The key product interpretation is:

> Abort Guard is a probe. It may not prevent the first steer-jerk, because the abort signal can arrive almost simultaneously with the jerk. Its job is to avoid feeding or repeating injection after the vehicle has entered abort.

### DouyinFSD reference

`/Users/ziwind/Codex/DouyinFSD /源代码与抓包数据/公开源代码.txt`

Useful ideas:

- Speed-limit segment based offset percentages.
- Absolute target-speed caps.
- Immediate speed-up following.
- Smooth speed-down at about `5 km/h/s`.

Rejected ideas for this design:

- New `0x399` writes.
- New `0x331` writes.
- New `0x3F8` writes.
- New `0x3FD` non-HW4 offset writes.
- Expanded `0x370` torque or hands-on injection.

### tesla-fsd-controller

`/Users/ziwind/my-vibe-project/tesla-fsd-controller`

Useful ideas:

- Conservative Legacy `0x2F8` offset write path.
- `0x2F8` sniffer diagnostics: seen, period, current raw offset, MPP raw.
- Independent Legacy speed-limit related toggles.
- Clear warnings when the expected Legacy frame is absent.

## 3. High-Level Architecture

Legacy Speed & Safety v2 is split into two independent modules plus existing handler integration.

```text
0x399 DAS_status
   |
   |-- fused limit raw --> LegacySmartOffsetEngine
   |                       |
   |                       |-- Off / Manual / Auto / Custom
   |                       |-- target-speed caps
   |                       |-- smooth speed-down
   |                       '-- output offset_kph 0..33
   |
   '-- AP state ---------> AbortGuardEngine
                           |
                           |-- default off, experimental
                           |-- latch on state 8/9
                           |-- clear on clean disengage
                           '-- exposes allowsInjection()

0x2F8 UI_gpsVehicleSpeed
   |
   |-- sniffer diagnostics
   '-- controlled write: data[5] low6 = offset_kph + 30

0x3EE FSD activation
   '-- existing AP-First + Soft Engage + optional Abort Guard block

0x438 / 0x3EE mux1
   '-- existing speed-limit helper paths + optional Abort Guard block
```

### New module: `LegacySmartOffsetEngine`

This pure algorithm module does not send CAN frames. It computes a final Legacy offset in km/h.

Output invariant:

```text
0 <= outputOffsetKph <= 33
```

The handler remains responsible for writing `0x2F8`:

```cpp
raw = outputOffsetKph + 30;
frame.data[5] = (frame.data[5] & 0xC0) | (raw & 0x3F);
```

### New module: `AbortGuardEngine`

This pure state machine does not send CAN frames. It observes AP state and answers whether injection is currently allowed.

Abort Guard is:

- Default off.
- Experimental.
- User enabled only.
- Latched by AP states `8` or `9`.
- Cleared only by clean disengage, defined as AP state `< 2`.

## 4. Legacy Smart Offset Algorithm

### Inputs

Vehicle/runtime inputs:

```text
fusedSpeedLimitRaw   # from 0x399 data[1] & 0x1F
speedLimitKph        # fusedSpeedLimitRaw * 5
apOrFsdEngaged       # current AP/FSD usage context
nowMs                # monotonic milliseconds
```

Valid speed-limit raw values:

```text
1..30  -> valid, unit = 5 km/h
0      -> unknown / no limit
31     -> SNA / none
>30    -> invalid
```

User configuration:

```text
mode                   # 0=off, 1=manual, 2=auto, 3=custom
manualOffsetKph         # 0..33
smoothDownEnabled       # default true
smoothDownRateKphS      # default 5, clamp 1..20
customPctLow            # default 50, clamp 0..63
customPctMid            # default 30, clamp 0..63
customPctHigh           # default 20, clamp 0..63
customPctVeryHigh       # default 10, clamp 0..63
```

### Mode behavior

#### Off

Output `0`. The `0x2F8` write path does not transmit.

#### Manual

Output `manualOffsetKph`, clamped to `0..33`.

#### Auto

Use built-in segment percentages and target caps.

Percentage table:

```text
limit <= 35 km/h    -> 63%
40..45 km/h         -> 50%
50..60 km/h         -> 50%
65..90 km/h         -> 30%
95..110 km/h        -> 20%
>110 km/h           -> 10%
```

Absolute target-speed cap table:

```text
<=35 -> 60
40   -> 60
45   -> 67
50   -> 75
55   -> 82
60   -> 90
70   -> 91
80   -> 104
90   -> 117
100  -> 120
110  -> 132
120  -> 132
other -> limit + 15
```

Computation:

```text
rawTargetSpeed = min(speedLimitKph * (1 + offsetPct / 100), absoluteCapKph)
desiredOffsetKph = rawTargetSpeed - speedLimitKph
outputOffsetKph = clamp(round(desiredOffsetKph), 0, 33)
```

#### Custom

Custom mode uses user percentages but the same target-cap and smoothing logic.

Percentage selection:

```text
limit <= 50       -> customPctLow
limit <= 70       -> customPctMid
limit <= 100      -> customPctHigh
limit > 100       -> customPctVeryHigh
```

### Smooth speed-down

Smoothing applies to target speed before converting back to offset.

Rules:

- First run: sync immediately.
- Target speed increases: sync immediately.
- AP/FSD not engaged: sync immediately.
- Target speed decreases while AP/FSD is engaged: reduce by at most `smoothDownRateKphS * dtSeconds`.
- If `dt <= 0` or `dt > 10s`: sync immediately.

Default rate:

```text
5 km/h/s
```

Hard bound:

```text
smoothedTargetKph <= speedLimitKph + 33
```

This respects the maximum positive range of `0x2F8 UI_userSpeedOffset`.

### Unknown / SNA speed limit

If the speed limit is unknown or SNA:

- Manual mode still uses the manual offset.
- Auto and Custom fall back to manual offset if it is nonzero.
- If fallback is zero, output zero and do not write `0x2F8`.

Diagnostics must state whether fallback was used.

### Diagnostics

`LegacySmartOffsetEngine` exposes:

```text
mode
speedLimitRaw
speedLimitKph
offsetPct
absoluteCapKph
rawTargetKph
smoothedTargetKph
outputOffsetKph
fallbackUsed
smoothingActive
lastUpdateMs
blockedReason
```

The `0x2F8` sniffer exposes:

```text
gpsSpeedSeen
gpsSpeedFresh
gpsSpeedPeriodMs
gpsUserOffsetRaw
gpsUserOffsetKph
gpsMppLimitRaw
gpsMppLimitKph
lastSentOffsetRaw
lastSentOffsetKph
txOk
txFail
```

## 5. Abort Guard State Machine

### Inputs

Abort Guard reads only:

```text
apState = 0x399.data[0] & 0x0F
```

State interpretation:

```text
3..6  -> active
8     -> aborting
9     -> aborted
<2    -> clean disengage / idle
other -> neither latch nor clear
```

### State machine

```text
DISABLED
  | abortGuardEnabled = true
  v
ARMED
  | apState == 8 || apState == 9
  v
LATCHED
  | apState < 2
  v
ARMED
```

### Disabled

- Default state.
- No injection blocking.
- May still record latest AP state for diagnostics.

### Armed

- User has enabled Abort Guard.
- `apState == 8` or `apState == 9` sets `latched = true`.
- Records `lastAbortState` and `latchedAtMs`.

### Latched

- `allowsInjection()` returns false.
- Injection paths using this guard must skip transmission.
- `apState < 2` clears the latch.
- State `6` must not clear a previous latch.

### Block scope

When enabled and latched, Abort Guard blocks:

- Legacy `0x3EE mux0` FSD activation.
- Legacy `0x3EE mux1` NAG / vision-speed-limit helper bits.
- Legacy `0x2F8` speed offset.
- Legacy `0x438` vision speed slider.
- Outer NAG/reactive/late-echo sending gates, without rewriting those engines' internal cooldown semantics.

Abort Guard should appear early in gate ordering so blocked diagnostics are clear:

```text
canActive
OTA guard
Abort Guard
AP injection gate
AP stable
Soft Engage
other path-specific gates
```

### Diagnostics

`/status.abortGuard` exposes:

```json
{
  "enabled": false,
  "latched": false,
  "lastApState": 6,
  "lastAbortState": 0,
  "latchedAtMs": 0,
  "lastClearReason": "none",
  "blocks": 0,
  "lastBlockedPath": "none"
}
```

Allowed `lastBlockedPath` values:

```text
none
legacy_fsd_mux0
legacy_fsd_mux1
legacy_speed_0x2f8
legacy_vision_slider_0x438
nag
```

## 6. Dashboard, API, and NVS

### Dashboard placement

Do not add a new major page.

Speed controls go into the existing speed strategy page. Defense controls go into the existing FSD Defense page.

### Speed strategy UI

Add a Legacy Smart Speed section:

```text
Legacy 智能速度偏移
  模式：关闭 / 手动 / 智能自动 / 自定义
  手动偏移：0..33 km/h
  降速平滑：开 / 关
  平滑速率：1..20 km/h/s

自定义百分比
  低速段 <=50： 50%
  中速段 <=70： 30%
  高速段 <=100：20%
  超高速 >100：10%

诊断
  当前限速
  原始限速 raw
  目标速度
  平滑目标
  输出偏移
  0x2F8 是否可见
  0x2F8 周期
  当前车端 offset raw
  最近发送 raw
```

Custom percentage fields should only be prominent in Custom mode. Diagnostics may be collapsed.

### Abort Guard UI

Add an experimental Defense card:

```text
Abort Guard（实验，默认关闭）
  [开关]

说明：检测到 DAS AP state 8/9 后锁止注入，直到 AP 完全退出。
这是实验探针，可能无法阻止首次甩方向盘，只用于避免异常后继续注入。

状态：关闭 / 已武装 / 已锁止
最近 AP state
最近 abort state
阻断次数
最近阻断路径
```

### `/config`

Extend the existing FSD runtime config block.

GET/POST shape:

```json
{
  "fsdRuntime": {
    "legacyOffset": 0,
    "legacyOffsetMode": 1,
    "legacySmoothDown": true,
    "legacySmoothRateKphS": 5,
    "legacyCustomPctLow": 50,
    "legacyCustomPctMid": 30,
    "legacyCustomPctHigh": 20,
    "legacyCustomPctVeryHigh": 10,
    "overrideSpeedLimit": false
  }
}
```

Mode encoding:

```text
0 = off
1 = manual
2 = auto
3 = custom
```

### `/defense_config`

Add Abort Guard config:

```json
{
  "abort_guard": false
}
```

Default is false.

### `/status`

Add:

```json
{
  "legacySpeed": {
    "mode": 2,
    "speedLimitRaw": 12,
    "speedLimitKph": 60,
    "offsetPct": 50,
    "absoluteCapKph": 90,
    "rawTargetKph": 90,
    "smoothedTargetKph": 84,
    "outputOffsetKph": 24,
    "fallbackUsed": false,
    "smoothingActive": true,
    "gpsSpeedSeen": true,
    "gpsSpeedFresh": true,
    "gpsSpeedPeriodMs": 100,
    "gpsUserOffsetRaw": 30,
    "gpsUserOffsetKph": 0,
    "gpsMppLimitRaw": 12,
    "gpsMppLimitKph": 60,
    "lastSentOffsetRaw": 54,
    "lastSentOffsetKph": 24,
    "txOk": 123,
    "txFail": 0,
    "blockedReason": "none"
  },
  "abortGuard": {
    "enabled": false,
    "latched": false,
    "lastApState": 6,
    "lastAbortState": 0,
    "latchedAtMs": 0,
    "lastClearReason": "none",
    "blocks": 0,
    "lastBlockedPath": "none"
  }
}
```

### NVS keys

Use readable short keys:

```text
lo_mode      # 0..3
lo_smooth    # bool
lo_rate      # 1..20
lo_p1        # 0..63
lo_p2        # 0..63
lo_p3        # 0..63
lo_p4        # 0..63
def_ag       # abort guard bool
```

Existing `legacyOffset` storage remains the manual offset source.

### Upgrade compatibility

Preserve default behavior:

```text
if legacyOffset > 0 and lo_mode missing -> Manual
if legacyOffset == 0 and lo_mode missing -> Off
```

Smart Auto never turns on automatically after upgrade.

### Settings export/import

Include:

```json
{
  "legacySpeed": {
    "mode": 2,
    "manualOffsetKph": 10,
    "smoothDown": true,
    "smoothRateKphS": 5,
    "customPctLow": 50,
    "customPctMid": 30,
    "customPctHigh": 20,
    "customPctVeryHigh": 10
  },
  "defense": {
    "abortGuard": false
  }
}
```

### Serial diagnostics

Add or extend serial commands:

```text
legacy_speed
abort_guard
```

Example output:

```text
=== Legacy Smart Speed ===
mode=auto
limitRaw=12 limitKph=60
pct=50 cap=90 rawTarget=90 smoothTarget=84
offset=24 gpsSeen=1 period=100
lastSentRaw=54 txOk=123 txFail=0 blocked=none

=== Abort Guard ===
enabled=0 latched=0 lastApState=6 lastAbortState=0
blocks=0 lastBlockedPath=none clear=none
```

## 7. Error Handling and Fail-Closed Rules

### Invalid speed limit

If speed-limit raw is `0`, `31`, or greater than `30`:

- Auto/Custom do not compute a new smart target.
- Fallback to manual offset if nonzero.
- Otherwise output zero.
- Diagnostics set `fallbackUsed` and an appropriate `blockedReason`.

### `0x2F8` absent

If `0x2F8` is never seen, no write can occur because the handler only modifies received `0x2F8` frames.

UI must show:

```text
0x2F8 未检测到：Legacy offset 可能不会生效
```

If `0x2F8` was seen but is stale, set `gpsSpeedFresh = false` for diagnostics.

### Abort Guard latched

Abort Guard latched blocks configured paths and increments block counters. It must not panic or reset the device.

### Time anomalies

For smoothing:

```text
dt <= 0  -> sync target directly
dt > 10s -> sync target directly
```

This avoids strange output after timer wrap, reset, or long CAN silence.

### Configuration anomalies

Clamp all API/NVS values:

```text
mode             0..3
manualOffsetKph  0..33
smoothRateKphS   1..20
customPct*       0..63
abortGuard       bool
```

Invalid NVS values fall back to defaults and must not produce unsafe output.

## 8. Explicit Safety Boundaries

This design explicitly forbids the following changes:

1. No new `0x399` writes.
2. No new `0x331` writes.
3. No new `0x3F8` writes.
4. No new `0x3FD` non-HW4 offset writes.
5. No expansion of `0x370` torque or hands-on injection.
6. No default-on Abort Guard.
7. No default-on Smart Auto after upgrade.

The only Legacy speed-offset write path in scope is:

```text
0x2F8 / 760 UI_gpsVehicleSpeed byte5 low 6 bits
```

## 9. Testing Plan

### Native tests: Legacy Smart Offset

Cover:

1. Off mode outputs zero.
2. Manual mode clamps to `0..33`.
3. Auto mode target caps for key limits: 35, 45, 60, 80, 100, 120.
4. Custom mode selects the correct percentage band.
5. Custom percentages clamp to `0..63`.
6. Unknown raw `0` falls back correctly.
7. SNA raw `31` falls back correctly.
8. Speed-up follows immediately.
9. Speed-down is rate limited.
10. AP/FSD not engaged disables smoothing.
11. Abnormal `dt` syncs directly.
12. Output never exceeds `33 km/h`.

### Native tests: Abort Guard

Cover:

1. Disabled guard does not block.
2. Enabled + state 6 does not block.
3. Enabled + state 8 latches.
4. Enabled + state 9 latches.
5. Latched + state 6 stays latched.
6. Latched + state 1 clears.
7. Disabled + state 8/9 does not latch.
8. Block count and last blocked path update correctly.

### Handler tests

Cover:

1. `0x2F8` writes preserve byte 5 bits 6-7.
2. Smart offset writes `raw = offset + 30`.
3. Output zero sends nothing.
4. Abort Guard latched blocks `0x2F8`.
5. Abort Guard latched blocks `0x3EE mux0`.
6. Abort Guard default off preserves current `0x3EE` behavior.
7. `0x399` remains read-only.
8. No new `0x331`, `0x3F8`, or `0x3FD` Legacy write path appears.

### API/UI contract tests

Cover:

1. `/config` GET includes new Legacy speed fields.
2. `/config` POST clamps parameters.
3. `/defense_config` GET/POST includes `abort_guard` default false.
4. `/status` includes `legacySpeed` and `abortGuard`.
5. UI includes text for 智能速度偏移, Abort Guard, 实验, 默认关闭, and `0x2F8 未检测到`.
6. Settings export/import includes new fields.
7. `minify_dashboard.py --check` passes.

### Full verification commands

Implementation plan should refine exact environment names, but the expected verification set is:

```bash
python3 -m unittest discover -s test -p 'test_*.py'
pio test -e native_legacy
pio test -e native_single_can_dashboard
pio test -e native_abort_guard
pio test -e native_legacy_speed
python3 scripts/minify_dashboard.py --check
pio run -e waveshare_single_can_standalone
```

Also run source searches proving forbidden paths were not added. These searches must be block-scoped or contract-test backed: a whole-file regex such as `frame.id == 921.*?driver.send(frame)` is too broad because it can match a read-only `0x399 / 921` branch and then continue across unrelated branches to a later allowed send. The final audit should inspect individual `if (frame.id == ...) { ... }` blocks, or use the existing `frame_id_blocks()`-style contract tests, so read-only observations are not reported as forbidden writes.

## 10. On-Car Validation Plan

### Step 1: Default state

- Smart Offset mode remains Off or Manual according to existing config.
- Abort Guard is off.
- No dashboard red warning.

### Step 2: Diagnostics only

Observe:

- `0x399` speed-limit raw.
- `0x2F8` seen / period.
- Current offset raw.
- Current MPP raw.

### Step 3: Manual mode

Set a small offset such as `+5 km/h`.

Expected:

```text
lastSentOffsetRaw = 35
txOk increases
no vehicle warning
```

### Step 4: Smart Auto

Use conservative defaults.

Observe:

- Correct limit-to-output behavior.
- Smooth speed-down behavior.
- No unexpected braking or alerts.

### Step 5: Abort Guard experiment

Only in a safe environment:

- Manually enable Abort Guard.
- If AP state becomes `8/9`, latch becomes true.
- Injection pauses.
- AP clean disengage clears latch.

Do not deliberately induce a dangerous steer-jerk.

## 11. Completion Criteria

The feature is complete only when all are true:

1. Native algorithm tests pass.
2. Abort Guard tests pass.
3. Handler tests pass.
4. API/UI contract tests pass.
5. Dashboard minify check passes.
6. ESP32 build succeeds.
7. Default behavior is unchanged after upgrade.
8. Source search proves no forbidden CAN write path was added.
9. Abort Guard is documented and implemented as experimental/off by default.
10. On-car validation steps are documented before any flash/use recommendation.
