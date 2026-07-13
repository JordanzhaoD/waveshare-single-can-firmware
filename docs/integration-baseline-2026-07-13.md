# v1.0.8 Single-CAN Integration Baseline — 2026-07-13

## Scope and starting point

This report records the unchanged v1.0.8 single-CAN baseline before integration product changes.

- Starting HEAD: `913f43cc08324ab6cac0da93e02c49e3c13b0aaf`
- Branch: `integration/v1.0.8-single-can-experiments-beta16`
- Working tree at start: clean
- Product-source changes made by this task: none
- Test method: baseline/evidence only; no artificial failing tests and no baseline repairs
- Shell note: this environment has `python3` but no `python`, so every required `python` command was run with `python3` as the exact interpreter equivalent.

## Ancestry and merge evidence

```bash
git merge-base --is-ancestor \
  aba16b656bce7c98e60c1e737a04eb7c5141c4be HEAD
```

Result: exit `0`; the v1.0.8 release commit is an ancestor of the starting HEAD.

```bash
git log --merges --oneline \
  aba16b656bce7c98e60c1e737a04eb7c5141c4be..HEAD
```

Result: exit `0`, no output. There are no merge commits in the inspected range, including no merge of `main`, human-replay, or dual-CAN branches.

## Toolchain versions

- Shell `python3`: Python `3.13.3`, executable `/opt/homebrew/bin/python3`
- PlatformIO Core: `6.1.18`, executable `/opt/homebrew/bin/pio`
- PlatformIO runtime Python: `3.13.3-final.0`, executable `/opt/homebrew/Cellar/platformio/6.1.18_1/libexec/bin/python`
- Git: `2.39.3 (Apple Git-145)`
- Host reported by PlatformIO: `darwin_arm64`, macOS `26.5.2`
- Firmware platform: `platformio/espressif32@7.0.0`
- Firmware framework: `framework-espidf@4.60001.0`
- Native platform: `native@1.2.1`
- Native test library installed by PlatformIO: Unity `2.6.1`
- ArduinoJson resolved for the firmware environment: `7.4.3`

PlatformIO printed this warning for every PlatformIO command:

```text
Obsolete PIO Core v6.1.18 is used (previous was 6.1.19)
Please remove multiple PIO Cores from a system
```

A supplementary `python3 -m platformio --version` probe failed with `No module named platformio`; the working `pio` command uses PlatformIO's isolated Homebrew Python shown above.

## Python and generated-UI checks

```bash
python3 -m unittest discover -s test -p 'test_*.py'
```

Result: exit `0`; `258` tests ran in total: `255` passed, `3` skipped, `0` failures, `0` errors.

```bash
python3 scripts/minify_dashboard.py --check
```

Result: exit `0`.

```text
html: 213720 -> 183351 bytes minified (85.8%)
gzip: 183351 -> 47881 bytes (26.1% of minified, 22.4% of original)
ui build: 1.0.6-waveshare_single_can_standalone-08a1e476ba10-2026-07-05T15:20:10Z (2026-07-05T15:20:10Z)
```

The source-derived payload and tracked generated header are synchronized. The tracked header carries pre-existing stamped build metadata identifying `1.0.6`; this task did not change it.

## Native PlatformIO tests

| Command | Result | Test cases |
|---|---:|---:|
| `pio test -e native_helpers` | PASS | `44/44` |
| `pio test -e native_injection_after_ap` | PASS | `15/15` |
| `pio test -e native_legacy` | PASS | `68/68` |
| `pio test -e native_reactive_nag` | PASS | `29/29` |
| `pio test -e native_epas_late_echo` | PASS | `52/52` |
| `pio test -e native_single_can_dashboard` | PASS | `11/11` |
| **Total** | **PASS** | **`219/219`** |

There were no native test failures.

## Waveshare firmware build

The existing single-CAN local profile was copied from the repository's primary worktree into this integration worktree as the gitignored `platformio_profile.h`. The source file was not modified, `cmp` confirmed the copy was byte-identical, and `git check-ignore -v platformio_profile.h` matched `.gitignore:247`. Its active configuration is `DRIVER_TWAI` plus `LEGACY`.

```bash
pio run -e waveshare_single_can_standalone
```

Result: exit `0`; `SUCCESS` in `31.899s`.

```text
HARDWARE: ESP32S3 240MHz, 320KB RAM, 16MB Flash
RAM:   [==        ]  15.1% (used 49408 bytes from 327680 bytes)
Flash: [===       ]  28.2% (used 1181743 bytes from 4194304 bytes)
========================= 1 succeeded in 00:00:31.899 =========================
```

Resolved build components included:

- Espressif32 platform `7.0.0`
- ESP-IDF `6.0.1` / framework package `4.60001.0`
- ArduinoJson `7.4.3`
- esptool.py `4.11.0`

The successful build emitted a pre-existing flash-geometry warning:

```text
Warning! Flash memory size mismatch detected. Expected 16MB, found 2MB!
Please select a proper value in your `sdkconfig.defaults` or via the `menuconfig` target!
```

No configuration or product-source repair was attempted in this baseline task.

## Generated-UI restoration evidence

The successful build stamped `include/web/mcp2515_dashboard_ui.h` with a v1.0.8 build ID and UTC timestamp. The required deterministic regeneration command was then run:

```bash
python3 scripts/minify_dashboard.py
git diff -- include/web/mcp2515_dashboard_ui.h
```

It reproduced the same payload sizes and changed the two metadata macros to:

```c
#define DASH_UI_BUILD_ID "manual"
#define DASH_UI_BUILD_UTC "manual"
```

Because the approved commit may contain only this baseline document and the starting tracked header carries pre-existing stamped metadata, the temporary generated-header change was restored to the exact starting-HEAD content. The source-derived payload check was rerun and the tracked diff was verified clean:

```bash
python3 scripts/minify_dashboard.py --check
git diff --exit-code -- include/web/mcp2515_dashboard_ui.h
```

Result: both exit `0`; no generated-UI diff remains.

## Baseline findings and pre-existing concerns

1. All required verification commands are green: Python ran `258` tests (`255` passed, `3` skipped), native PlatformIO ran `219/219` test cases successfully, the generated-UI payload check passed, and the Waveshare firmware build succeeded.
2. Firmware usage is RAM `15.1%` (`49,408 / 327,680` bytes) and app flash `28.2%` (`1,181,743 / 4,194,304` bytes).
3. The build reports 16MB hardware but also warns that the active generated/configured flash size is detected as 2MB. This baseline records the mismatch without repairing it.
4. PlatformIO reports Core `6.1.18` as obsolete and notes that `6.1.19` was previously used, indicating multiple-Core/toolchain drift on the host.
5. The synchronized tracked dashboard header contains pre-existing `1.0.6` stamped metadata even though this is the v1.0.8 integration baseline. Deterministic regeneration changes only those metadata macros to `manual`; no generated-header change is included in this task.

## Baseline conclusion

The unchanged v1.0.8 baseline is established: Python, dashboard synchronization, all six required native environments, and the Waveshare firmware build pass with the approved `DRIVER_TWAI + LEGACY` local profile. The recorded pre-existing concerns are the 16MB-versus-2MB flash-geometry warning, PlatformIO Core drift, and stale tracked generated-UI metadata; none was repaired by this evidence-only task.
