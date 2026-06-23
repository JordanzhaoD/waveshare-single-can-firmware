# Steer-Jerk on FSD Injection — Data-Driven Diagnosis (CN 2026.8.3.6 HW3)

> For: upstream `hypery11/flipper-tesla-fsd#108` / `ev-open-can-tools#66`, and reporter `@dunckencn`.
> Captures: `ids_3EE_399_488_118_145-{normal,steerjerkerror-4,steerjerkerror-16}.txt`.
> Analysis scripts: `scratch/steer-jerk/analyze_candump.py`, `compare.py`.
>
> 中文要点（给 Jordan）：报告者两份 jerk 日志 + 一份 normal 参考已解码比对。0x488 是
> 方向盘指令帧，b0b1 = 角度×10。normal 全程 Δ4°，两份 jerk 分别在 AP 激活后 +0.26s /
> +0.20s 出现 Δ248° / Δ179° 暴跳（与报告者读数吻合），随后 0.3–0.5s 内 AP 故障退出。
> 根因 = 激活边沿注入 0x3EE bit46；我们固件另有 status-6 不识别 + 门控默认关两个放大
> 因素。建议 AP-First（已验证降到 <5%）+ 边沿软化（开放前沿），2026.8.3.6 暂 Listen-Only。

## 1. Decode confirmation

`0x488` = `DAS_steeringControl` (the steering *command* AP issues). Bytes 0–1 form a
signed 16-bit value in units of **0.1°** (steering angle × 10).

| Log | 0x488 value range | Largest frame-to-frame step | In degrees |
|---|---|---|---|
| normal | 16154–16558 | 41 | **Δ4°** |
| steerjerkerror-4 | 16134–18809 | **2483** | **Δ248°** |
| steerjerkerror-16 | 14854–18220 | **1793** | **Δ179°** |

The raw steps (2483, 1793) reproduce the reporter's observed Δ248° / Δ179° to within
rounding. In *normal* the command never moves more than Δ4°; in both jerk logs it spikes
40–60× larger. **This is the jerk, measured at the source.**

## 2. Timeline (relative to AP engage, `0x399` → state 6)

| Event | normal | jerk-4 | jerk-16 |
|---|---|---|---|
| AP engage (`0x399`→6) | 5.35 s | 3.94 s | 16.03 s |
| `0x488` steering spike | — | **+0.26 s** | **+0.20 s** |
| AP fault (`0x399`→8/9) | none (holds 9 s) | +0.74 s | +0.43 s |

**The spike lands ~0.2 s after AP engagement** — i.e., right at the moment AP takes over
the steering wheel (the "activation edge"), **not** ~1 s after as estimated. AP then
fault-disengages within 0.2–0.5 s.

Causal chain:

```
AP engage (0x399→6) ── ~0.2 s ──▶ 0x488 command spikes Δ179–248° (the jerk)
                                   └── 0.2–0.5 s ──▶ 0x399→8/9 fault, AP aborts
```

## 3. Note on injection visibility

`0x3EE bit46` (the FSD-enable bit) is **never observed set** in any of the three logs —
including *normal*, where FSD did activate. The captures are RX-only; the injector's TX
(the modified `0x3EE` with `bit46=1`) is not echoed into the RX stream. So the injection
itself is invisible here; what we see is the **car's response**. The next capture should
enable TWAI listen-loopback (TX echo) so the injected frame and its timing relative to the
car's own `0x3EE` are visible — that will let us confirm the activation-edge model and
evaluate edge-softening.

## 4. Root cause

**Activation-edge injection.** Injecting the FSD-enable step (`0x3EE` mux0 `bit46` 0→1)
inside the ~0.2 s window where AP takes over steering disrupts the takeover handshake on
China 2026.8.3.6 (whose autonomy preflight is tightened — see #108: another user's FSD
stopped engaging entirely after this update). AP then issues an out-of-range steering
command (`0x488` spike) → EPS jerk → AP fault. This is the same activation-edge transient
upstream already identified; AP-First (inject only after AP is stably engaged) is the
correct mitigation and the data is consistent with it.

## 5. Two amplifier defects (shared across forks in this lineage)

1. **AP-state detection misses state 6.** The AP-active helper only recognizes DAS states
   3–5. This car engages to **state 6**, so any logic keying off "AP engaged" (including
   the AP-First settle timer) never arms. Suggested fix: treat state 6 as active
   (`3..6`); states 8 (handover) / 9 (fault) remain inactive. Worth checking upstream —
   this likely affects the ESP32 build too.
2. **AP-First gate off by default.** Where the gate is gated behind a compile flag and
   that flag is off, Legacy `0x3EE` injection fires ungated and can land on the edge.

## 6. Recommendations

- AP-First (engage AP first, inject only after stable ≈1–2 s) is the right primary
  mitigation; the reporter's own data (~95 % success) confirms it.
- Residual <5 % is the open frontier. Hypothesis to test next: **soften the enable edge**
  (ramp/sequence `bit46` rather than a hard step), but only after a TX-echo capture
  confirms the mechanism.
- For CN 2026.8.3.6 specifically: keep the car in **Listen-Only** until a verified fix
  ships; research/educational use only; user assumes all risk.
- Next capture (full-rate, tight filter, **TX echo on**):
  `?ids=3EE,488,399,39B,118,370` — so the injected `0x3EE` and the car's own are both
  visible and can be diffed across the jerk.

## 7. Reproduction artifacts

- Logs: see zip `ids_3EE_399_488_118_145-steerjerkerror.zip`.
- Decode/compare scripts: `scratch/steer-jerk/{analyze_candump.py,compare.py}`.
- Fix design (our fork): `docs/superpowers/specs/2026-06-23-steer-jerk-ap-injection-fix-design.md`.
