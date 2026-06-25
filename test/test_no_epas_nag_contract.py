#!/usr/bin/env python3
"""Guard rail: EPAS-faithful nag injection must stay REMOVED.

DashEpasNagEngine injected spoofed 0x370 (EPAS own-status) frames and caused a
real-vehicle body-control fault (2026-06-19). It was physically removed. This
test prevents re-introducing that hazard class by asserting the forbidden
symbols are absent from compiled source.

Run: python3 -m pytest test/test_no_epas_nag_contract.py -q
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Symbols that exist ONLY for the removed DashEpasNagEngine path.
# Legacy NagHandler / nagKillerRuntime / bit-19 baseline do NOT match these.
FORBIDDEN = [
    "DashEpasNag",       # engine struct + diag
    "dashEpasNag",       # global instance / wiring
    "epasNagDiag",       # /status JSON object
    "tryBuildEcho",      # engine echo builder
    "nextDemandTorque",  # engine demand-state machine
    "EpasNagTraceRing",  # trace recorder
    "dash_epas_nag",     # include filename
    "def_epnag",         # NVS key
]

# Scanned COMPILED source only (never test/ — this file itself contains the words).
SCAN_DIRS = [ROOT / "include", ROOT / "src"]
SCAN_FILES = [ROOT / "platformio.ini"]
SRC_EXTS = (".h", ".hpp", ".c", ".cpp", ".ino")


def _source_text():
    chunks = []
    for d in SCAN_DIRS:
        for p in d.rglob("*"):
            if p.is_file() and p.suffix in SRC_EXTS:
                chunks.append(p.read_text(errors="ignore"))
    for f in SCAN_FILES:
        if f.exists():
            chunks.append(f.read_text(errors="ignore"))
    return "\n".join(chunks)


def test_no_epas_nag_symbols_in_compiled_source():
    src = _source_text()
    hits = {sym: src.count(sym) for sym in FORBIDDEN if sym in src}
    assert not hits, (
        "EPAS-faithful nag symbols must stay REMOVED (2026-06-19 safety incident). "
        f"Found: {hits}. See docs/EPAS-NAG-REMOVAL-INCIDENT.md — do NOT re-add "
        "0x370 echo injection; it faults the power-steering ECU."
    )


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


def test_legacy_bionic_steering_is_optin_and_gated():
    """LegacyHandler bionic (0x370 sine-on-0x08B6 echo) must be opt-in: bionicSteering
    defaults false, the bionic echo is gated on bionicSteering + handsOn==0, and the 8
    banned DashEpasNag symbols stay absent (DashBionicSteer is not one of them)."""
    h = (ROOT / "include" / "handlers.h").read_text()

    # bionicSteering member defaults false (on CarManagerBase)
    assert re.search(r"Shared<bool>\s+bionicSteering\s*\{[^}]*\bfalse\b", h), \
        "bionicSteering must default to false"

    # LegacyHandler has the bionic echo gated on bionicSteering + handsOn==0
    assert "bool useBionic = (bool)bionicSteering && !bionic.isDisabled() && handsOn == 0" in h, \
        "LegacyHandler bionic echo must be gated on bionicSteering + handsOn==0"

    # bionic uses the 0x08B6 tamper base + sine (DashBionicSteer.applyToFrame)
    assert "bionic.applyToFrame(" in h
    assert "0x08B6" in (ROOT / "include" / "dash_bionic_steer.h").read_text()

    # 8 banned DashEpasNag symbols still absent from handlers.h
    for sym in FORBIDDEN:
        assert sym not in h, f"banned symbol {sym} must stay out of handlers.h"
