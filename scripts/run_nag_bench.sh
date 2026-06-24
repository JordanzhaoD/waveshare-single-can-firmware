#!/usr/bin/env bash
# Surface the [REAL-DATA] findings from the NagHandler real-data bench.
#
# PlatformIO's native test adapter suppresses non-Unity stdout, so the printf()
# findings emitted by T3/T4 in test/test_native_nag/test_nag_handler.cpp do NOT
# appear in `pio test -e native_nag` output. This helper rebuilds the suite
# (which still reports PASS/FAIL), then runs the compiled binary directly so
# the [REAL-DATA] lines are visible.
set -euo pipefail
cd "$(dirname "$0")/.."

pio test -e native_nag >/dev/null

BIN="$(find .pio/build/native_nag -type f -name program ! -name "*.o" | head -1)"
if [ -z "$BIN" ]; then
    echo "ERROR: native_nag binary not found under .pio/build/native_nag" >&2
    exit 1
fi

echo "=== NagHandler real-data bench findings ==="
"$BIN" 2>&1 | grep "\[REAL-DATA\]" || echo "(no [REAL-DATA] findings printed)"
