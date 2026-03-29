#!/bin/bash
# Run iSH with profiling enabled
# Usage: profile-run.sh [ish-args...]

set -e

PROFILE_OUTPUT="${ISH_PROFILE_OUTPUT:-ish-profile.json}"
ISH_BIN="${ISH_BIN:-./builddir/ish}"

echo "=== iSH Profiling Run ==="
echo "Output: $PROFILE_OUTPUT"
echo "Command: $ISH_BIN $@"
echo ""

# Run with profiling
ISH_PROFILE_OUTPUT="$PROFILE_OUTPUT" "$ISH_BIN" "$@"

echo ""
echo "=== Profile saved to $PROFILE_OUTPUT ==="
echo "Analyze with: meson compile profile-analyze"
echo "Or: python3 tools/ish-profile-tool.py analyze $PROFILE_OUTPUT"
