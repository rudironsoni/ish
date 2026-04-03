#!/bin/bash

set -euo pipefail

# Normalize Xcode environment to match a regular shell invocation of Meson.
readonly CLEAN_ENV=(
    -u PYTHONHOME
    -u PYTHONPATH
    -u PYTHONEXECUTABLE
    -u __PYVENV_LAUNCHER__
)

run_clean() {
    env "${CLEAN_ENV[@]}" "$@"
}

# Ensure Homebrew bin is in PATH for finding ninja
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

resolve_binary() {
    local tool="$1"
    local candidate
    for candidate in "$(command -v "$tool" 2>/dev/null)" "/opt/homebrew/bin/$tool" "/usr/local/bin/$tool"; do
        if [[ -n "$candidate" && -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

# Resolve Meson CLI once.
MESON_BIN=""
if MESON_BIN=$(resolve_binary meson); then
    :
elif command -v python3 >/dev/null 2>&1 && run_clean python3 -c 'import mesonbuild' >/dev/null 2>&1; then
    MESON_BIN="python3 -m mesonbuild"
else
    echo "error: Meson is required but was not found. Install 'meson' or the Python 'mesonbuild' package." >&2
    exit 127
fi

# Resolve ninja binary path
NINJA_BIN=""
if ! NINJA_BIN=$(resolve_binary ninja); then
    echo "error: ninja is required but was not found. Install ninja via Homebrew." >&2
    exit 127
fi

# Export NINJA so Meson can find it
export NINJA="$NINJA_BIN"

run_meson() {
    if [[ "$MESON_BIN" == *"python3"* ]]; then
        run_clean $MESON_BIN compile "$@"
    else
        run_clean "$MESON_BIN" compile "$@"
    fi
}

# Use meson compile (official Meson build command) instead of raw ninja.
# This delegates build semantics to Meson while still using ninja under the hood.
run_meson "$@"
