#!/usr/bin/env python3
"""Guard rail: reactive NAG-suppression is opt-in + bounded; NagHandler tamper stays opt-in.

History: the old DashEpasNagEngine (spoofed 0x370 EPAS own-status frames) caused a
real-vehicle body-control fault (2026-06-19) and was physically removed. Jordan
authorized on 2026-06-25: (1) lift the 0x370 symbol-ban, (2) ignore the Mode-C
incident — to enable porting the public-source reactive torque-burst technique
(DashReactiveNagBurst). This file now guards the FORWARD-looking safety contract
for that reactive path (opt-in, bounded, amplitude-capped) plus the existing
NagHandler torque-tamper opt-in guard.

Run: python3 -m pytest test/test_no_epas_nag_contract.py -q
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def test_no_native_epas_nag_env():
    ini = (ROOT / "platformio.ini").read_text()
    assert "[env:native_epas_nag]" not in ini, "native_epas_nag env must stay removed"


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


def test_reactive_nag_is_optin_and_bounded():
    """DashReactiveNagBurst (0x370 reactive torque burst) must be opt-in and bounded:
    bionicSteering defaults false, the burst is gated on bionicSteering + shouldInject,
    0x399 NAG detection is read-only, amplitude is capped, and the burst is bounded
    (max bursts + cooldown). Replaces the lifted 0x370 symbol-ban (Jordan 2026-06-25
    authorized lifting the ban + ignoring the Mode-C incident)."""
    h = (ROOT / "include" / "handlers.h").read_text()
    rn = (ROOT / "include" / "dash_reactive_nag.h").read_text()

    # bionicSteering member defaults false (on CarManagerBase)
    assert re.search(r"Shared<bool>\s+bionicSteering\s*\{[^}]*\bfalse\b", h), \
        "bionicSteering must default to false"

    # LegacyHandler 0x370 echo gated on bionicSteering + nag.shouldInject
    assert "bool useReactive = (bool)bionicSteering && APActive && nag.shouldInject(nowMs)" in h, \
        "reactive echo must be gated on bionicSteering + APActive + nag.shouldInject"

    # 0x399 NAG detection reads byte5 bits[5:2] (read-only, no forge)
    assert "(frame.data[5] >> 2) & 0x0F" in h, "0x399 handsOn decode must read byte5 bits[5:2]"

    # DashReactiveNagBurst exists with bounded burst + amplitude cap
    assert "DashReactiveNagBurst" in rn
    assert "kMaxBursts{3}" in rn, "burst must be bounded (max 3)"
    assert "kCooldownMs{3000}" in rn, "cooldown must be enforced (3 s)"
    assert "kAmplitudeCap{95}" in rn, "amplitude hard cap must exist"
    assert "kHumanWeight{8}" in rn
