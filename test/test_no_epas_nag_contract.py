#!/usr/bin/env python3
"""Guard rail: EPAS-faithful nag injection must stay REMOVED.

DashEpasNagEngine injected spoofed 0x370 (EPAS own-status) frames and caused a
real-vehicle body-control fault (2026-06-19). It was physically removed. This
test prevents re-introducing that hazard class by asserting the forbidden
symbols are absent from compiled source.

Run: python3 -m pytest test/test_no_epas_nag_contract.py -q
"""
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
