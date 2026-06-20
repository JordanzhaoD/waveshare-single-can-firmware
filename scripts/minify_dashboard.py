#!/usr/bin/env python3
"""Minify + gzip embedded dashboard HTML in mcp2515_dashboard_ui.h.

Reads source HTML from include/web/mcp2515_dashboard_ui.src.h, minifies
HTML/CSS/JS, gzips the result, and writes mcp2515_dashboard_ui.h with:
  - DASH_HTML[]   (raw minified HTML, for native builds and debugging)
  - DASH_HTML_GZ[] / DASH_HTML_GZ_LEN  (gzip-compressed for HTTP send)
  - DASH_UI_BUILD_ID / DASH_UI_BUILD_UTC diagnostics
"""
from __future__ import annotations

import argparse
import gzip
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import csscompressor
import htmlmin
import rjsmin

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
DST = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"


def terser_minify(code: str) -> str:
    terser = shutil.which("terser")
    if not terser:
        return rjsmin.jsmin(code)
    proc = subprocess.run(
        [terser, "--compress", "--ecma", "2020"],
        input=code,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        print(f"warn: terser failed: {proc.stderr}", file=sys.stderr)
        return rjsmin.jsmin(code)
    return proc.stdout


def minify_blocks(html: str, tag: str, fn) -> str:
    pattern = re.compile(rf"(<{tag}\b[^>]*>)(.*?)(</{tag}>)", re.DOTALL | re.IGNORECASE)

    def repl(match):
        try:
            return match.group(1) + fn(match.group(2)) + match.group(3)
        except Exception as exc:
            print(f"warn: {tag} minify failed: {exc}", file=sys.stderr)
            return match.group(0)

    return pattern.sub(repl, html)


def c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def hex_array(data: bytes, width: int = 16) -> str:
    out = []
    for i in range(0, len(data), width):
        chunk = data[i : i + width]
        out.append(",".join(f"0x{b:02x}" for b in chunk))
    return ",\n    ".join(out)


def extract_html(text: str) -> str:
    m = re.search(r'R"HTML\((.*)\)HTML";', text, re.DOTALL)
    if not m:
        raise ValueError("no HTML payload found")
    return m.group(1)


def render_header(src: Path = SRC, build_id: str = "unknown", build_utc: str = "unknown") -> tuple[str, int, int, int]:
    text = src.read_text(encoding="utf-8")
    html = extract_html(text)
    before = len(html)

    html = minify_blocks(html, "style", csscompressor.compress)
    html = minify_blocks(html, "script", terser_minify)
    html = htmlmin.minify(
        html,
        remove_comments=True,
        remove_empty_space=True,
        remove_all_empty_space=True,
        reduce_boolean_attributes=True,
        keep_pre=True,
    )

    raw_len = len(html)
    gz = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
    gz_len = len(gz)

    body = []
    body.append("#pragma once")
    body.append("#ifdef ESP_PLATFORM")
    body.append('#include "platform/espidf_runtime.h"')
    body.append("#else")
    body.append("#include <Arduino.h>")
    body.append("#endif")
    body.append("#include <stddef.h>")
    body.append("#include <stdint.h>")
    body.append("")
    body.append("#ifndef DASH_UI_BUILD_ID")
    body.append(f'#define DASH_UI_BUILD_ID "{c_string(build_id)}"')
    body.append("#endif")
    body.append("#ifndef DASH_UI_BUILD_UTC")
    body.append(f'#define DASH_UI_BUILD_UTC "{c_string(build_utc)}"')
    body.append("#endif")
    body.append("")
    body.append("#ifndef ESP_PLATFORM")
    body.append('static const char DASH_HTML[] PROGMEM = R"HTML(' + html + ')HTML";')
    body.append("#endif")
    body.append("")
    body.append("static const uint8_t DASH_HTML_GZ[] PROGMEM = {")
    body.append("    " + hex_array(gz))
    body.append("};")
    body.append(f"static constexpr size_t DASH_HTML_GZ_LEN = {gz_len};")
    body.append("")

    return "\n".join(body), before, raw_len, gz_len


def read_header_define(text: str, name: str, default: str = "unknown") -> str:
    m = re.search(rf'#define\s+{re.escape(name)}\s+"([^"]*)"', text)
    return m.group(1) if m else default


def write_header(dst: Path = DST, src: Path = SRC, build_id: str | None = None, build_utc: str | None = None, check: bool = False) -> int:
    current = dst.read_text(encoding="utf-8") if dst.exists() else ""
    if check:
        # Reuse the existing diagnostic metadata so --check compares source UI
        # content, not the naturally-changing build timestamp.
        build_id = build_id or read_header_define(current, "DASH_UI_BUILD_ID")
        build_utc = build_utc or read_header_define(current, "DASH_UI_BUILD_UTC")
    else:
        build_id = build_id or os.environ.get("DASH_UI_BUILD_ID", "manual")
        build_utc = build_utc or os.environ.get("DASH_UI_BUILD_UTC", "manual")
    body, before, raw_len, gz_len = render_header(src=src, build_id=build_id, build_utc=build_utc)
    if check:
        if current != body:
            print(f"{dst.relative_to(ROOT)} is stale; run scripts/minify_dashboard.py", file=sys.stderr)
            return 1
    else:
        current = dst.read_text(encoding="utf-8") if dst.exists() else None
        if current != body:
            dst.write_text(body, encoding="utf-8")
    print(f"html: {before} -> {raw_len} bytes minified ({100 * raw_len / before:.1f}%)")
    print(
        f"gzip: {raw_len} -> {gz_len} bytes ({100 * gz_len / raw_len:.1f}% of minified, "
        f"{100 * gz_len / before:.1f}% of original)"
    )
    print(f"ui build: {build_id} ({build_utc})")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated header is stale")
    parser.add_argument("--build-id", default=None)
    parser.add_argument("--build-utc", default=None)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return write_header(build_id=args.build_id, build_utc=args.build_utc, check=args.check)
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
