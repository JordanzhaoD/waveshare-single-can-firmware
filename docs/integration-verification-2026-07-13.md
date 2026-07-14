# Single-CAN Integration Verification

Date: 2026-07-14

## Scope

This report verifies the completed Waveshare single-CAN integration built from the published `v1.0.8-atlas-single-can` baseline and including the recovered four-mode Legacy NAG model plus functional v2.16-beta.16 Instant Engage behavior.

- Branch: `integration/v1.0.8-single-can-experiments-beta16`
- Product implementation HEAD before this documentation task: `a061de93a789b92248cc78a5199580966906b81c`
- Integration base: `aba16b656bce7c98e60c1e737a04eb7c5141c4be`
- Working tree before Task 16: clean
- Verification host date: 2026-07-14
- Python command used: `python3` (the host has no `python` executable, as recorded in the baseline report)

## Git boundary

The following ancestry check passed:

```bash
git merge-base --is-ancestor \
  aba16b656bce7c98e60c1e737a04eb7c5141c4be HEAD
```

The following protected-file comparison exited successfully with no diff:

```bash
git diff --exit-code \
  aba16b656bce7c98e60c1e737a04eb7c5141c4be...HEAD \
  -- partitions_16mb_ota_4096k_nvs64.csv \
     .github/workflows/release.yml \
     .github/workflows/auto-tag-release.yml
```

`git log --merges --oneline aba16b6..HEAD` produced no output. No branch merge was introduced.

The files newly added relative to the integration base are limited to the integration reports/design/plan and focused single-CAN helpers:

```text
docs/integration-baseline-2026-07-13.md
docs/integration-preservation-2026-07-13.md
docs/superpowers/plans/2026-07-13-v216-beta16-single-can-integration.md
docs/superpowers/specs/2026-07-13-v216-beta16-single-can-integration-design.md
include/dash_ap_first_gate.h
include/dash_config_update.h
include/dash_legacy_370_echo.h
include/dash_nag_diag.h
include/dash_nag_mode.h
include/dash_reactive_hold_nag.h
```

No newly added path matched `dual`, `lilygo`, `can2`, `partition`, or `workflow`. No LILYGO dual-CAN runtime, controller, UI, packaging, partition, or release-workflow file was imported.

## Python results

```bash
python3 -m unittest discover -s test -p 'test_*.py'
```

Result: exit `0`.

```text
Ran 269 tests in 0.259s
OK (skipped=3)
```

- Total: `269`
- Passed: `266`
- Skipped: `3`
- Failures: `0`
- Errors: `0`

## Native matrix

All native environments required by the approved Task 16 plan exist in `platformio.ini` and passed.

| Environment | Result | Test cases |
|---|---:|---:|
| `native` | PASS | `101/101` |
| `native_dashboard` | PASS | `11/11` |
| `native_single_can_dashboard` | PASS | `11/11` |
| `native_log_buffer` | PASS | `8/8` |
| `native_mcp2515_recovery` | PASS | `2/2` |
| `native_nag` | PASS | `34/34` |
| `native_injection_after_ap` | PASS | `40/40` |
| `native_plugin_engine` | PASS | `25/25` |
| `native_hw3` | PASS | `57/57` |
| `native_hw4` | PASS | `45/45` |
| `native_legacy` | PASS | `79/79` |
| `native_legacy_speed` | PASS | `15/15` |
| `native_abort_guard` | PASS | `8/8` |
| `native_helpers` | PASS | `49/49` |
| `native_twai` | PASS | `23/23` |
| `native_reactive_nag` | PASS | `52/52` |
| `native_epas_late_echo` | PASS | `52/52` |
| `native_wheel_dnd` | PASS | `15/15` |
| **Command total** | **PASS** | **`627/627`** |

The `native` aggregate overlaps several focused environments, so `627` is the sum of executed command test cases rather than a unique-test count.

## UI determinism

Before the firmware build:

```bash
python3 scripts/minify_dashboard.py --check
```

Result: exit `0`.

After the build stamped the generated header, deterministic regeneration and recheck both passed:

```bash
python3 scripts/minify_dashboard.py
python3 scripts/minify_dashboard.py --check
```

Final deterministic output:

```text
html: 217021 -> 186345 bytes minified (85.9%)
gzip: 186345 -> 48524 bytes (26.0% of minified, 22.4% of original)
ui build: manual (manual)
```

The human-maintained `mcp2515_dashboard_ui.src.h` and generated `mcp2515_dashboard_ui.h` are synchronized.

## Firmware build and size

```bash
pio run -e waveshare_single_can_standalone
```

Result: `SUCCESS` in `10.714s`.

```text
HARDWARE: ESP32S3 240MHz, 320KB RAM, 16MB Flash
RAM:   15.1% (49408 / 327680 bytes)
Flash: 28.4% (1193023 / 4194304 bytes)
```

Resolved key toolchain components:

- PlatformIO Core `6.1.18`
- `platformio/espressif32@7.0.0`
- ESP-IDF `6.0.1` (`framework-espidf@4.60001.0`)
- ArduinoJson `7.4.3`
- esptool.py `4.11.0`

### Known environment warnings

The successful build retained two previously recorded environment/toolchain warnings:

1. PlatformIO Core `6.1.18` reports that `6.1.19` was previously used and warns about multiple Core installations.
2. The build reports `Expected 16MB, found 2MB` for the generated/configured flash geometry even though the board profile reports 16MB hardware.

Task 16 did not modify `sdkconfig.defaults`, partition CSV files, board configuration, or release workflows to address these pre-existing warnings.

## Functional coverage

### Human Replay TSL6P

- Stable mode value: `1`.
- Existing TSL6P burst/replay framing remains selected explicitly rather than being conflated with other algorithms.
- Legacy integration, counter/checksum, gate, and diagnostics regressions pass.

### EPAS Late Echo

- Stable mode value: `2`.
- Cadence-aware delayed echo remains separate from immediate TSL6P/Reactive Hold framing.
- Pending-frame cancellation, abort/fault, `checkAD`, and source-frame behavior regressions pass.

### Reactive Hold

- Stable mode value: `3`.
- Restored as `Reactive Sustained Hold` with distinct proactive and reactive phases.
- Output bounds, positive hold behavior, cooldown, wraparound, mode switching, gate blocking, successful-send accounting, and `0x399` read-only behavior are covered.
- `bionicSteering` remains a historical parent enable and is not exposed as a fifth algorithm.

### Instant Engage

- API field: `ap_first_edge`.
- NVS key: `apfe`.
- Default: `false`.
- Backup/restore field: `device.apFirstEdge`.
- State `2` remains non-engaged; states `3..6` are engaged.
- Only a genuine non-engaged → engaged edge can bypass an unsatisfied Legacy AP-First delay, and the bypass is one-shot.
- Sustained engagement cannot create repeated edges or repeated bypasses.
- Parent AP gate disable preserves the saved Instant Engage preference while clearing transient timing.
- Instant Engage does not bypass CAN/FSD enablement, OTA blocking, parent AP gate, `checkAD`, gear logic, Abort Guard, Soft Engage, plugins, or feature enablement.
- `/status.fsdDiag.gate` and serial `system_status` expose runtime-consumed edge/debounce/bypass evidence, including wrap-safe missing-edge age handling.

## Protected work audit

Protected refs still match the preservation manifest:

```text
main = bce7c91568b6689b3763647a1f8bff9400f37daf
feature/human-replay-nag-v3 = a71b902c1d1753c1d0b5127d424208dc8ac223a7
release/v1.0.8-beta15-safety-hotfix = f76d181e499fe149ebf26957725f580916df9135
```

The original checkout retains its pre-existing dirty state, including `docs/OPERATION_MANUAL.pdf` and the recorded untracked docs, assets, and captures. The linked beta15 worktree still contains its two recorded tracked modifications:

```text
M sdkconfig.defaults
M test/test_dashboard_api_contract.py
```

All `52` binary/capture entries recorded with size and SHA-256 in `docs/integration-preservation-2026-07-13.md` were rechecked against the original repository:

```text
protected_assets_checked=52
protected_asset_errors=0
```

The protected original `docs/OPERATION_MANUAL.pdf` remains present with current SHA-256:

```text
b24ef68a72172d2e06bf88bf401b22876354450215571087be4c71a1dc65fbde
```

It was not modified or staged by this integration worktree. Task 16 updates only the Markdown operating manual.

## Documentation updates

The current product behavior is now documented in:

- `CHANGELOG.md` under `Unreleased`;
- the Chinese and English feature/runtime sections of `README.md`;
- `docs/OPERATION_MANUAL.md`;
- `docs/dashboard.md`.

The documentation states the four modes precisely, identifies `bionicSteering` as the historical parent enable, describes Reactive Hold phases, documents Instant Engage persistence and safety boundaries, and distinguishes historical incident context from current opt-in behavior.

## Final checks

- `git diff --check`: PASS.
- v1.0.8 ancestry: PASS.
- Protected partition/release workflow comparison: PASS.
- Merge-history check: PASS, no merge commits in the integration range.
- Dual-CAN added-file audit: PASS, no excluded product files added.
- Python suite: PASS.
- Complete native matrix: PASS.
- Dashboard deterministic generation: PASS.
- Waveshare standalone build: PASS.
- Protected refs/assets/worktrees: present and verified.

## Not performed

The following actions were intentionally not performed:

- Push
- Merge or cherry-pick from another branch
- Tag or release
- Firmware asset publication
- Flash
- OTA
- Vehicle test
- Partition CSV modification
- Release workflow modification
- Protected PDF regeneration
