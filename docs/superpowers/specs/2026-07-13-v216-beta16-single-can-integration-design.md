# v2.16-beta.16 Single-CAN Integration Design

Date: 2026-07-13
Project: Waveshare Single CAN Firmware
Upstream: `hypery11/flipper-tesla-fsd` release `v2.16-beta.16`
Approved approach: Rebuild by functional slices from the published `v1.0.8-atlas-single-can` baseline

## Goal

Create a new Waveshare single-CAN integration line that combines:

1. the complete published `v1.0.8-atlas-single-can` product baseline;
2. all already-implemented, independently valuable single-CAN experiments from the local `main` and `feature/human-replay-nag-v3` lines;
3. a working local implementation of upstream `v2.16-beta.16` Instant Engage.

The integration must preserve the stable features delivered in v1.0.5 through v1.0.8, retain the final user-visible capabilities of the local single-CAN experiments, and implement the intended Instant Engage behavior rather than copying upstream ESP32 plumbing that persists the setting without consuming it in the compiled gate.

## Approved Product Scope

### Included

- Published v1.0.8 Waveshare single-CAN product behavior.
- Reactive and bionic NAG behavior already implemented on local single-CAN branches.
- Distinct human-replay behavior that has production code and tests.
- Existing single-CAN diagnostics, configuration, Dashboard wiring, and native tests required by those experiments.
- Upstream beta.16 Instant Engage:
  - runtime configuration;
  - persistence;
  - Dashboard and API wiring;
  - actual AP-First gate consumption;
  - diagnostics;
  - behavior tests.
- Documentation updates required to describe the integrated behavior and supersede stale policy statements.

### Excluded

- LILYGO dual-CAN phase 1-4 code, runtime, controllers, Dashboard surfaces, and product packaging.
- Direct whole-branch merge of the current experimental `main`.
- Mechanical replay of every historical intermediate commit.
- Upstream Flipper scenes, unrelated hardware targets, OTA changes, partition changes, or release packaging changes.
- Release creation, tag creation, push, firmware asset publication, device flashing, or vehicle testing.
- Implementation of ideas that exist only as unimplemented specs or plans.

## Verified Upstream Delta

The exact upstream comparison is:

- `v2.16-beta.15`: `1b0dab6bef1b5d77d772463eff4ffb1c8a4e19fa`
- functional commit: `bdf9c78c6ffcb0753cb07ff4306585d77b10f48d`
- `v2.16-beta.16`: `ec2e85d0a9dd601e2c5d4eb1695123232e1d393c`
- compare: <https://github.com/hypery11/flipper-tesla-fsd/compare/v2.16-beta.15...v2.16-beta.16>

The delta contains nine modified files and one product feature: experimental Instant Engage. Upstream adds an `ap_first_edge` setting that allows a genuine AP engaged edge to bypass the normal one-second AP-First debounce while continuing to reject state 2.

The upstream ESP32 release persists and displays `ap_first_edge`, but the ESP32 C++ gate compiled by PlatformIO does not consume it. The shared/Flipper C gate does. This local integration targets functional parity with the advertised feature and therefore must connect the setting to the local runtime gate.

No beta.16 counter, checksum, OTA, partition, hardware pin, or release workflow changes are in scope.

## Current Local Context

The repository has materially divergent local product lines.

### Current main

- Branch: `main`
- HEAD: `bce7c91568b6689b3763647a1f8bff9400f37daf`
- Relative to the saved `origin/main`: ahead 38, behind 10.
- Approximately 33 commits contain local semantics not present in the saved remote line.
- Local product version is still `1.0.4`.
- Main contains reactive/bionic NAG work and beta.15 regression work.

### Published product baseline

- Tag: `v1.0.8-atlas-single-can`
- Peeled commit: `aba16b656bce7c98e60c1e737a04eb7c5141c4be`
- Contains the complete stable v1.0.5-v1.0.8 product progression.
- Is the approved integration base.

### Other protected local work

- `feature/human-replay-nag-v3@a71b902`
- `release/v1.0.8-beta15-safety-hotfix@f76d181`
- Dirty release worktree files:
  - `sdkconfig.defaults`
  - `test/test_dashboard_api_contract.py`
- Modified `docs/OPERATION_MANUAL.pdf`
- Approximately 80 untracked docs, release assets, and scratch captures.

These lines and files are preservation inputs, not merge targets.

## Branch and Preservation Strategy

Create a new integration branch from `v1.0.8-atlas-single-can`, not from the current checkout.

Before implementation, record a preservation manifest containing:

- branch names and full HEAD SHAs;
- registered worktree paths and dirty state;
- tracked dirty files;
- untracked file paths and sizes;
- checksums for irreplaceable capture and release asset files;
- the exact v1.0.8 base SHA.

The implementation must not use:

- `git reset --hard` on an existing local line;
- `git clean -fd` or `git clean -fdx`;
- broad `git add .`;
- whole-line rebase of current `main`;
- deletion of linked worktrees;
- aggressive Git prune or garbage collection.

Only exact intended paths may be staged.

## Integration Architecture

The new line has three conceptual layers:

```text
Layer 1: Published v1.0.8 product baseline
    |
Layer 2: Implemented single-CAN experimental feature slices
    |
Layer 3: Working v2.16-beta.16 Instant Engage
```

### Layer 1: v1.0.8 baseline

Preserve all stable product behavior, including:

- EPAS late echo;
- Abort Guard;
- Legacy smart speed offset;
- touch-oriented UI controls;
- disclaimer flow;
- mobile navigation and plugin name fixes;
- beta.15 AP engaged semantics and timestamp underflow fix;
- release and CI structure already used by the published product.

### Layer 2: single-CAN experimental slices

Build a feature matrix before porting. Each row must record:

- feature name;
- source branch and source commits;
- user-visible behavior;
- production source files;
- runtime configuration and NVS fields;
- Dashboard/API surface;
- diagnostics;
- tests;
- relationship to later experiments;
- conflict points against v1.0.8.

Classify each candidate as follows:

| Candidate type | Integration rule |
|---|---|
| Production code plus tests | Port as a feature slice |
| Distinct selectable behavior | Preserve as a separate mode |
| Earlier implementation superseded by a later superset | Preserve the final capability, not duplicate intermediate code |
| Docs/spec only | Reference material, not an automatic implementation requirement |
| Dual-CAN-specific | Exclude from this product line |

The target is complete final user capability, not preservation of every historical implementation stage.

### Layer 3: Instant Engage

Add Instant Engage only after the stable baseline and local experiment slices are green. This keeps upstream timing behavior separate from feature-recovery conflicts.

## Component Design

### 1. AP-First gate module

The AP engaged edge and debounce state must be owned by a small testable module rather than duplicated across Dashboard globals and handler conditions.

Conceptual interface:

```cpp
struct ApFirstDecision {
    bool engaged;
    bool edgeDetected;
    bool debounceSatisfied;
    bool instantBypass;
    bool allowed;
};

ApFirstDecision updateApFirstGate(
    uint8_t apState,
    bool apGateEnabled,
    bool instantEngageEnabled,
    uint32_t debounceMs,
    uint32_t nowMs
);
```

The final name and file should follow the existing local naming style. The module may live alongside current CAN helpers if it remains small and pure, or in a focused header if state ownership would otherwise deepen an existing large file.

Required semantics:

- state 2 is not engaged;
- states 3 through 6 are engaged;
- state 8, state 9, unknown, and non-engaged states are not engaged;
- disabled Instant Engage preserves the local configurable AP delay (`0..3000ms`, v1.0.8 default `2000ms`); the upstream beta.16 reference uses one second, but this integration must not change the published local default;
- enabled Instant Engage bypasses the debounce only on a genuine non-engaged-to-engaged edge;
- a sustained engaged state does not generate repeated edges;
- disengage, abort/fault, AP gate disable, or runtime reset clears edge/debounce state;
- unsigned elapsed calculations remain wrap-safe.

Instant Engage changes only the debounce decision. It must not bypass unrelated existing conditions such as the CAN/FSD master switch, hardware mode, gear logic, abort/fault cooldown, Soft Engage, or per-feature enablement.

Only paths already consuming the AP-First gate inherit the new timing behavior. The integration must not silently move unrelated send paths into or out of that gate.

### 2. Runtime configuration and persistence

Add a boolean runtime setting aligned with upstream naming:

```text
API field: ap_first_edge
NVS key: apfe
Default: false
```

Requirements:

- missing NVS key loads as `false`;
- valid configuration updates runtime state and persistence together;
- invalid values leave the existing setting unchanged and return an explicit failure;
- reboot restores the saved value;
- disabling the parent AP gate does not erase the saved Instant Engage preference;
- runtime reset clears transient edge/debounce state, not the saved preference.

### 3. Dashboard backend

Integrate the field into the existing local configuration flow rather than adding a parallel endpoint.

Required surfaces:

- configuration GET includes `ap_first_edge`;
- configuration POST accepts `ap_first_edge`;
- `/status` reports the effective saved setting and diagnostics;
- serial diagnostics, if they already expose AP gate state, should include the same runtime counters without creating a separate subsystem.

### 4. Dashboard UI

Add a child toggle next to the existing AP delayed-injection/AP gate controls:

```text
Instant Engage (experimental)
Allow the first eligible injection immediately when AP truly becomes engaged.
```

UI requirements:

- load from backend state;
- save through the existing configuration path;
- show that the setting is currently inactive when the parent AP gate is disabled;
- do not overwrite the saved value merely because the parent is disabled;
- preserve existing desktop and mobile layouts;
- update the human-maintained `mcp2515_dashboard_ui.src.h`, then regenerate the compiled header using the existing minifier;
- do not hand-merge the generated UI header.

### 5. Diagnostics

Expose the minimum evidence required to prove that the setting affects runtime behavior:

```text
instantEngageEnabled
apEngaged
apEdgeCount
lastApEdgeAgeMs
apDebounceBypassCount
```

The exact JSON nesting should match existing status conventions. Missing timestamps must not render as unsigned-wrap ages.

These diagnostics distinguish:

- saved configuration;
- actual engaged-edge detection;
- actual debounce bypass;
- blocking by another existing gate.

### 6. Experimental NAG modes

Reactive, bionic, proactive, sustained-hold, and human-replay behavior must be represented by one coherent mode model.

Integration rules:

- distinct behaviors remain selectable;
- shared counter/checksum/frame-building logic is reused rather than copied;
- configuration migration preserves existing user selections where fields are compatible;
- diagnostics use one common schema with mode-specific counters only where needed;
- final mode names and defaults must be derived from already-implemented local behavior, not invented from plan-only documents;
- existing 0x370, torque, and hands-on functionality is treated as normal feature scope and is not automatically excluded by historical policy text.

## Data Flow

### AP state to send decision

```text
CAN AP-state frame
    -> local AP-state decoder
    -> AP-First gate state update
    -> current handler and feature gates
    -> existing frame modification/send path
    -> diagnostic counters
```

### Configuration to runtime

```text
Dashboard control
    -> existing configuration POST
    -> validation
    -> runtime setting
    -> NVS key apfe
    -> AP-First gate
    -> status/diagnostic output
```

### Experimental mode selection

```text
Dashboard mode selection
    -> existing NAG configuration path
    -> normalized mode enum/config
    -> selected state machine
    -> shared frame builder
    -> existing CAN driver path
```

## Error and Edge Handling

- Unknown or malformed AP state does not create an engaged edge.
- AP state 2 remains blocked even when Instant Engage is enabled.
- State 8/9 resets the edge and debounce state immediately.
- Invalid configuration requests do not silently coerce arbitrary values to false.
- Missing NVS data uses documented defaults.
- NVS write failure must be surfaced through the existing configuration error channel and must not report a successful persisted update.
- A UI save response is not accepted as proof of runtime behavior; native tests and bypass diagnostics provide that proof.
- Generated UI drift is caught by `minify_dashboard.py --check`.
- Historical documentation may remain as incident history, but current policy and current behavior must be stated unambiguously.

## Implementation Phases

### Phase 0: preservation and baseline

1. Create the preservation manifest.
2. Establish the new integration branch from the v1.0.8 tag commit.
3. Run the full v1.0.8 baseline suite before product changes.
4. Record any pre-existing failures before proceeding.

### Phase 1: feature matrix

1. Compare v1.0.8, current `main`, and `feature/human-replay-nag-v3` by behavior.
2. Separate implemented features from plan-only ideas.
3. Resolve superseded versus distinct modes.
4. Define the exact slice order and touched files.

### Phase 2: recover local single-CAN experiments

Recommended dependency order:

1. pure helpers and state models;
2. reactive/bionic core state machine;
3. Legacy handler integration;
4. distinct human-replay modes;
5. runtime configuration and NVS;
6. Dashboard API;
7. UI;
8. diagnostics;
9. docs and contract updates.

Each slice must use test-first development and leave the branch green before the next slice begins.

### Phase 3: Instant Engage

1. Add failing pure gate tests.
2. Implement edge/debounce state.
3. Add handler integration tests.
4. Add configuration and persistence tests.
5. Add Dashboard/API/UI wiring.
6. Add diagnostics and contract tests.
7. Run the full regression suite.

### Phase 4: final verification

1. Run all Python tests.
2. Run every existing native CI environment.
3. Verify deterministic UI generation.
4. Build the Waveshare production firmware.
5. Run `git diff --check`.
6. Confirm protected branches, worktrees, dirty files, and untracked assets remain intact.
7. Review the complete integration diff for stable-feature regressions and accidental dual-CAN imports.

## Testing Plan

### Baseline

Before porting, run the v1.0.8 suite and record results.

### Per feature slice

Run:

- the feature's pure/native tests;
- Legacy handler integration tests when a send path changes;
- affected Dashboard/API Python contracts;
- affected configuration persistence tests.

### Instant Engage matrix

Required cases:

```text
Instant disabled + state 3       -> wait the configured AP delay (v1.0.8 default 2000ms)
Instant enabled + state 2        -> remain blocked
Instant enabled + transition 2-3 -> first eligible decision bypasses debounce
Instant enabled + sustained 3    -> no repeated edge creation
Instant enabled + transition 3-8 -> reset immediately
Instant enabled + disengage-3    -> create a new edge
Other gate disabled              -> Instant Engage does not bypass it
uint32 time wrap                 -> debounce remains correct
Missing apfe key                 -> false
Saved apfe + reboot              -> saved value restored
```

Also test that the runtime bypass counter changes only when the debounce was actually bypassed.

### Final commands

At minimum:

```bash
python -m unittest discover -s test -p 'test_*.py'
pio test -e native
pio test -e native_dashboard
pio test -e native_log_buffer
pio test -e native_mcp2515_recovery
pio test -e native_nag
pio test -e native_injection_after_ap
pio test -e native_plugin_engine
pio test -e native_hw3
pio test -e native_hw4
pio test -e native_legacy
pio test -e native_helpers
pio test -e native_twai
pio test -e native_reactive_nag
pio test -e native_wheel_dnd
python scripts/minify_dashboard.py --check
pio run -e waveshare_single_can_standalone
git diff --check
```

If the integration adds a new focused native environment, it must also be added to the CI matrix.

## Completion Criteria

The implementation is complete only when:

- the integration branch descends from the published v1.0.8 tag;
- v1.0.8 stable user-visible behavior has no known regression;
- all implemented, independently valuable single-CAN experimental capabilities are present;
- superseded intermediate implementations are not duplicated;
- no dual-CAN product code was accidentally imported;
- Instant Engage is configurable, persisted, visible, consumed by the runtime gate, diagnosable, and covered by tests;
- state 2 remains non-engaged and states 3-6 remain engaged;
- the full Python/native/build matrix is green or every pre-existing failure is explicitly documented;
- generated UI is deterministic and synchronized with its source;
- protected local branches, worktrees, dirty files, assets, and captures remain intact;
- the branch has not been pushed, released, tagged, flashed, or vehicle-tested.

## Implementation Boundary

Approval of this design authorizes writing an implementation plan, not implementation itself. The next step is to create a task-level TDD plan that identifies the exact feature slices, source commits, target files, tests, and verification command for each task.
