# Single-CAN Integration Preservation Manifest

Date: 2026-07-13

## Protected refs
- main: `bce7c91568b6689b3763647a1f8bff9400f37daf`
- feature/human-replay-nag-v3: `a71b902c1d1753c1d0b5127d424208dc8ac223a7`
- release/v1.0.8-beta15-safety-hotfix: `f76d181e499fe149ebf26957725f580916df9135`
- Integration base: `aba16b656bce7c98e60c1e737a04eb7c5141c4be`
- Preservation/design baseline: `9f2256c6d7501390dca2c2dc76677484b14f4218`
- Task-start tip: `97518b200276cd35c1b2044ba272b16d9d95eef5`

The integration branch is based on `aba16b656bce7c98e60c1e737a04eb7c5141c4be`. The preservation/design baseline `9f2256c6d7501390dca2c2dc76677484b14f4218` and the Task 1 starting tip `97518b200276cd35c1b2044ba272b16d9d95eef5` are later descendants of that integration base.

### Documentation-only commits from preservation/design baseline to task-start tip

- `89403eb5afffaed9c81bd8bae350c602bdec55a2` — `docs: plan beta16 single-CAN integration`
  - Added: `docs/superpowers/plans/2026-07-13-v216-beta16-single-can-integration.md`
- `97518b200276cd35c1b2044ba272b16d9d95eef5` — `docs: align beta16 plan with local AP delay`
  - Modified: `docs/superpowers/plans/2026-07-13-v216-beta16-single-can-integration.md`
  - Modified: `docs/superpowers/specs/2026-07-13-v216-beta16-single-can-integration-design.md`

The commit subjects and file scopes above were verified with `git log` and `git diff-tree`; both commits affect documentation only.

## Dirty tracked files
- `docs/OPERATION_MANUAL.pdf`
- `.claude/worktrees/beta15-safety-hotfix/sdkconfig.defaults`
- `.claude/worktrees/beta15-safety-hotfix/test/test_dashboard_api_contract.py`

## Untracked assets and captures

Each entry records the byte size and SHA-256 observed in the original repository without changing the file.

- `release-assets-epas-late-echo-20260701/bootloader.bin` — 18576 bytes — `c0900cecc0d4243623d88dd6f8d8f92be2a7bde83d06e400adf8c44205349009`
- `release-assets-epas-late-echo-20260701/firmware-waveshare-single-can.bin` — 1179696 bytes — `9f188e100228493dd7d9148d34c70de546a7b5aa59bf3b88e13161c66e34c24d`
- `release-assets-epas-late-echo-20260701/firmware.bin` — 1179696 bytes — `9f188e100228493dd7d9148d34c70de546a7b5aa59bf3b88e13161c66e34c24d`
- `release-assets-epas-late-echo-20260701/merged-flash.bin` — 1310768 bytes — `214df70f9e41e3577a77163da428aff37c10c728d64036a9a11a881dc11821d9`
- `release-assets-epas-late-echo-20260701/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets-epas-late-echo-20260701/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `release-assets-fix-20260705/bootloader.bin` — 18576 bytes — `dda9d6f78c74a17aeba00afb7ab08cae5e75944af649ffe7ac0f1956f60cfbd3`
- `release-assets-fix-20260705/firmware.bin` — 1197008 bytes — `4c2430b037f005002ad3db36559b5e30c211296f106674a17712d4ef341a96d6`
- `release-assets-fix-20260705/merged-flash (1.07).bin` — 1328080 bytes — `c574a03ff69eedf840c42ced373cf67e268f949eed7d43f5fa5f053d3463c260`
- `release-assets-fix-20260705/merged-flash.bin` — 1328080 bytes — `39f5e0b8410b68a2ffe39b0bb1ba79c629ceb08b92760fb3df4d6ce948624012`
- `release-assets-fix-20260705/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets-fix-20260705/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `release-assets-human-replay-v3/bootloader.bin` — 18576 bytes — `52395e01a2ec02bde15e6163560fb731c9303394d0ec8a5846c9a5c8a14c342d`
- `release-assets-human-replay-v3/firmware-waveshare-single-can.bin` — 1170416 bytes — `cf81aff45e28bd8a346eda13698fa90fd47dbed66fdfd74287366a6a7fdeb21a`
- `release-assets-human-replay-v3/firmware.bin` — 1170416 bytes — `cf81aff45e28bd8a346eda13698fa90fd47dbed66fdfd74287366a6a7fdeb21a`
- `release-assets-human-replay-v3/merged-flash.bin` — 1301488 bytes — `8a869aa0ba3ddf7a8475f5dbf6f9254a4d34cc7d3005fe2fb903321e6584563e`
- `release-assets-human-replay-v3/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets-human-replay-v3/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `release-assets-reactive-nag-v2/bootloader.bin` — 18576 bytes — `6ad48329d4d3f2107c344af51b5b4548b47070a88954a4a82d5ce2efcbec3ee7`
- `release-assets-reactive-nag-v2/firmware-waveshare-single-can.bin` — 1172128 bytes — `26703e4a7cf190e3497d383d42d62bb0285bbb1b04f38a08bd5ebb5f58677c8b`
- `release-assets-reactive-nag-v2/firmware.bin` — 1172128 bytes — `26703e4a7cf190e3497d383d42d62bb0285bbb1b04f38a08bd5ebb5f58677c8b`
- `release-assets-reactive-nag-v2/merged-flash.bin` — 1303200 bytes — `b090591335306004a2c6b774dbdd84daa5ed0e3ff43cbdbb9fc278a92625853d`
- `release-assets-reactive-nag-v2/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets-reactive-nag-v2/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `release-assets-reactive-nag/bootloader.bin` — 18576 bytes — `ec237c254e8f7ea48d907b29a453867eb345b2f403fa34a86485a0852df89a30`
- `release-assets-reactive-nag/firmware-waveshare-single-can.bin` — 1171552 bytes — `0c7c3658954e61b56294e814b19dd1e8ef40d263bcd3f015cb1f2ab3b41a8f2a`
- `release-assets-reactive-nag/firmware.bin` — 1171552 bytes — `0c7c3658954e61b56294e814b19dd1e8ef40d263bcd3f015cb1f2ab3b41a8f2a`
- `release-assets-reactive-nag/merged-flash.bin` — 1302624 bytes — `919760db4c0c5eec217913389e803a06e3830cdb4865dd314e13a2f19c43fd18`
- `release-assets-reactive-nag/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets-reactive-nag/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `release-assets-tsl6p-burst-v4/bootloader.bin` — 18576 bytes — `9cee91d73968a28a9dd9134989c1f70a2633d2e1b35f8c55ed34b6250ef82108`
- `release-assets-tsl6p-burst-v4/firmware-waveshare-single-can.bin` — 1172768 bytes — `3ab1aee442f0c3461879bf49cd17b19343eb8b1326b45d44f71f0e091cd7d386`
- `release-assets-tsl6p-burst-v4/firmware.bin` — 1172768 bytes — `3ab1aee442f0c3461879bf49cd17b19343eb8b1326b45d44f71f0e091cd7d386`
- `release-assets-tsl6p-burst-v4/merged-flash.bin` — 1303840 bytes — `c8030ff3e6d5d5df4dda804cb98f5cb223f6cf98f24a2bff60a8cd75bf7f8e34`
- `release-assets-tsl6p-burst-v4/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets-tsl6p-burst-v4/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `release-assets-ui-unify-20260703/bootloader.bin` — 18576 bytes — `41977e07deef11bde259d04f511d127710513ca5cc645f60021a396bdee127d6`
- `release-assets-ui-unify-20260703/firmware-waveshare-single-can.bin` — 1196560 bytes — `28050a37d27d94b9795ade53bfcf49c03cffc8229ee3b8b3e4412e6e9195fbfa`
- `release-assets-ui-unify-20260703/firmware.bin` — 1196560 bytes — `28050a37d27d94b9795ade53bfcf49c03cffc8229ee3b8b3e4412e6e9195fbfa`
- `release-assets-ui-unify-20260703/merged-flash (1).bin` — 1327632 bytes — `19735ec457c28e245afaa61176088dc0639e977f11221be25c63940ce60aba09`
- `release-assets-ui-unify-20260703/merged-flash.bin` — 1327632 bytes — `53f576453cf4240c1491a42793d186e8c2a99b6558aac720da6c30b3b75ef19e`
- `release-assets-ui-unify-20260703/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets-ui-unify-20260703/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `release-assets/bootloader.bin` — 18576 bytes — `3d1cd4467ac2c5851c7ee0b919e1640e555fa8ad31629047ade69152a5996ebd`
- `release-assets/firmware-waveshare-single-can.bin` — 1169104 bytes — `b894fb2583381bf47d7097e55fb5ae60f94a6c54a50085f7dbd19f62833ffb97`
- `release-assets/firmware.bin` — 1169104 bytes — `b894fb2583381bf47d7097e55fb5ae60f94a6c54a50085f7dbd19f62833ffb97`
- `release-assets/merged-flash.bin` — 1300176 bytes — `2bb40ed00409e196de23d168b42a3928e80ea8c7f3db7241266f0c120b90f9d2`
- `release-assets/ota_data_initial.bin` — 8192 bytes — `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `release-assets/partitions.bin` — 3072 bytes — `d7cca4968173011e2563bc81b8969ff183af13b2e494ed61ec4016d007d8e946`
- `scratch/soft-engage-merged.bin` — 1295536 bytes — `a878383c05484642b19cfaa86aa9ac37f09348238f7fa47f78118d38a263f191`
- `scratch/steer-jerk/jerk-3.csv` — 46223 bytes — `361afae6fc5b41504e6d234b0965ed3f264d134d2b186498cd9e171d9680bc47`
- `scratch/steer-jerk/jerk-4.csv` — 61296 bytes — `ec531489b52dc85a5e608e953df1a3a42889bd18f8ac60a09d2be6ceaf844e13`

## Read-only command evidence

### Original repository refs

```text
$ git rev-parse main
bce7c91568b6689b3763647a1f8bff9400f37daf

$ git rev-parse feature/human-replay-nag-v3
a71b902c1d1753c1d0b5127d424208dc8ac223a7

$ git rev-parse release/v1.0.8-beta15-safety-hotfix
f76d181e499fe149ebf26957725f580916df9135

$ git rev-parse integration/v1.0.8-single-can-experiments-beta16
97518b200276cd35c1b2044ba272b16d9d95eef5
```

### Worktree inventory

```text
$ git worktree list --porcelain
worktree /Users/ziwind/my-vibe-project/waveshare-single-can-firmware
HEAD bce7c91568b6689b3763647a1f8bff9400f37daf
branch refs/heads/main

worktree /Users/ziwind/my-vibe-project/waveshare-single-can-firmware/.claude/worktrees/agent-abdfef94fb375a70b
HEAD d00e2bfb7245bd46f92587bc297cfc076539f4f9
branch refs/heads/worktree-agent-abdfef94fb375a70b
locked claude agent agent-abdfef94fb375a70b (pid 26594 start Fri Jul 10 15:02:10 2026)

worktree /Users/ziwind/my-vibe-project/waveshare-single-can-firmware/.claude/worktrees/beta15-safety-hotfix
HEAD f76d181e499fe149ebf26957725f580916df9135
branch refs/heads/release/v1.0.8-beta15-safety-hotfix

worktree /Users/ziwind/my-vibe-project/waveshare-single-can-firmware/.claude/worktrees/beta16-single-can-integration
HEAD 97518b200276cd35c1b2044ba272b16d9d95eef5
branch refs/heads/integration/v1.0.8-single-can-experiments-beta16
```

### Original repository status

```text
$ git status --short --branch
## main...origin/main [ahead 38, behind 10]
 M docs/OPERATION_MANUAL.pdf
?? docs/HANDOFF-2026-06-25.md
?? docs/research/
?? docs/superpowers/plans/2026-06-27-tsl6p-burst-nag-implementation.md
?? docs/superpowers/plans/2026-06-29-epas-faithful-late-echo-nag-implementation.md
?? docs/superpowers/plans/2026-07-02-ui-unify-disclaimer-implementation.md
?? docs/superpowers/plans/2026-07-13-v216-beta16-single-can-integration.md
?? docs/superpowers/specs/2026-06-27-tsl6p-burst-nag-design.md
?? docs/superpowers/specs/2026-07-02-ui-unify-disclaimer-design.md
?? docs/superpowers/specs/2026-07-13-v216-beta16-single-can-integration-design.md
?? release-assets-epas-late-echo-20260701/
?? release-assets-fix-20260705/
?? release-assets-human-replay-v3/
?? release-assets-reactive-nag-v2/
?? release-assets-reactive-nag/
?? release-assets-tsl6p-burst-v4/
?? release-assets-ui-unify-20260703/
?? release-assets/
?? scratch/soft-engage-merged.bin
?? scratch/steer-jerk/analyze.py
?? scratch/steer-jerk/jerk-3.csv
?? scratch/steer-jerk/jerk-4.csv
?? scratch/steer-jerk/real/
```

### Linked beta15 worktree status

```text
$ git status --short --branch
## release/v1.0.8-beta15-safety-hotfix...origin/main [ahead 69]
 M sdkconfig.defaults
 M test/test_dashboard_api_contract.py
```

### Isolated execution checkout

```text
$ git branch --show-current
integration/v1.0.8-single-can-experiments-beta16

$ git rev-parse HEAD
97518b200276cd35c1b2044ba272b16d9d95eef5

$ git merge-base --is-ancestor aba16b656bce7c98e60c1e737a04eb7c5141c4be HEAD
(exit 0)

$ git merge-base --is-ancestor 9f2256c6d7501390dca2c2dc76677484b14f4218 HEAD
(exit 0)
```

## Prohibited cleanup
- No reset --hard
- No clean -fd/-fdx
- No prune/GC
- No linked-worktree deletion
