#!/usr/bin/env python3
"""Register dashboard UI generation as a PlatformIO/SCons dependency.

This script used to rewrite the source UI as an import side effect. That made
`mcp2515_dashboard_ui.h` newer than compiled objects without forcing a rebuild,
so firmware could serve an old dashboard. It now exposes the generated header as
an explicit SCons target and makes the firmware depend on it.
"""
from __future__ import annotations

import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_FILE = Path(globals().get("__file__", "scripts/update_ota_build_timestamp.py"))
if SCRIPT_FILE.is_absolute():
    ROOT = SCRIPT_FILE.resolve().parent.parent
else:
    ROOT = Path.cwd().resolve()
SCRIPT_FILE = ROOT / "scripts" / "update_ota_build_timestamp.py"
SRC = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
DST = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"
MINIFY = ROOT / "scripts" / "minify_dashboard.py"
VERSION_FILE = ROOT / "VERSION"


def _git_short_sha() -> str:
    try:
        proc = subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=str(ROOT),
            text=True,
            capture_output=True,
            check=True,
        )
        return proc.stdout.strip() or "nogit"
    except Exception:
        return "nogit"


def _build_metadata(env_name: str | None = None) -> tuple[str, str]:
    now_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    version = VERSION_FILE.read_text(encoding="utf-8").strip() if VERSION_FILE.exists() else "unknown"
    env_part = env_name or os.environ.get("PIOENV") or "manual"
    build_id = f"{version}-{env_part}-{_git_short_sha()}-{now_utc}"
    return build_id, now_utc


def generate(build_id: str | None = None, build_utc: str | None = None) -> int:
    if not build_id or not build_utc:
        build_id, build_utc = _build_metadata()
    cmd = [
        sys.executable,
        str(MINIFY),
        "--build-id",
        build_id,
        "--build-utc",
        build_utc,
    ]
    return subprocess.run(cmd, cwd=str(ROOT)).returncode


def register_scons() -> bool:
    try:
        from SCons.Script import Import
    except Exception:
        return False

    try:
        Import("env")
    except Exception:
        return False

    # Import() injects env into locals/globals in SCons scripts.
    env = globals().get("env") or locals().get("env")
    if env is None:
        return False

    build_id, build_utc = _build_metadata(env.get("PIOENV"))

    def action(target, source, env):  # noqa: ANN001 - SCons callback signature
        cmd = [
            sys.executable,
            str(MINIFY),
            "--build-id",
            build_id,
            "--build-utc",
            build_utc,
        ]
        return subprocess.run(cmd, cwd=str(ROOT)).returncode

    sources = [str(SRC), str(VERSION_FILE), str(MINIFY), str(SCRIPT_FILE)]
    ui_header = env.Command(str(DST), sources, action)

    # The dashboard HTML is pulled into the firmware through
    # include/web/mcp2515_dashboard.h, which is included from main.cpp. Make the
    # generated header visible to SCons so a UI change rebuilds the object and
    # links a new firmware image instead of only rewriting the header on disk.
    env.Depends("$BUILD_DIR/src/main.cpp.o", ui_header)
    env.Depends("$BUILD_DIR/${PROGNAME}.elf", ui_header)
    env.Depends("$BUILD_DIR/${PROGNAME}.bin", ui_header)
    return True


if __name__ == "__main__":
    raise SystemExit(generate())

if not register_scons():
    _result = generate()
    if _result:
        raise SystemExit(_result)
