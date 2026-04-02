#!/bin/bash

set -euo pipefail

# Normalize Xcode environment to match a regular shell invocation of Meson.
# Xcode injects variables that can poison Homebrew Python/Meson startup.
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

resolve_xcrun_tool() {
    local tool="$1"
    local candidate
    candidate=$(env -u SDKROOT -u IPHONEOS_DEPLOYMENT_TARGET xcrun --find "$tool" 2>/dev/null) || return 1
    [[ -x "$candidate" ]] || return 1
    printf '%s\n' "$candidate"
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

run_meson() {
    if [[ "$MESON_BIN" == *"python3"* ]]; then
        run_clean $MESON_BIN "$@"
    else
        run_clean "$MESON_BIN" "$@"
    fi
}

if ! host_clang=$(resolve_xcrun_tool clang); then
    echo "error: Could not resolve clang from Xcode toolchain." >&2
    exit 127
fi

if ! host_ar=$(resolve_xcrun_tool ar); then
    echo "error: Could not resolve ar from Xcode toolchain." >&2
    exit 127
fi

mkdir -p "$MESON_BUILD_DIR"
cd "$MESON_BUILD_DIR"

# Check if already configured via meson introspect.
if ! config=$(run_meson introspect --buildoptions); then
    # Initial setup: write cross file and run meson setup.
    export CC_FOR_BUILD="$host_clang"
    export CC="$CC_FOR_BUILD"
    crossfile=cross.txt
    arch_args=""
    for arch in $ARCHS; do
        arch_args="'-arch', '$arch', $arch_args"
    done
    arch_args="${arch_args%%, }"
    meson_arch=${ARCHS%% *}
    case "$meson_arch" in
        arm64) meson_arch=aarch64 ;;
    esac
    cat > "$crossfile" <<-EOF
    [binaries]
    c = '$host_clang'
    ar = '$host_ar'

    [host_machine]
    system = 'darwin'
    cpu_family = '$meson_arch'
    cpu = '$meson_arch'
    endian = 'little'

    [built-in options]
    c_args = [$arch_args]
    
    [properties]
    needs_exe_wrapper = true
EOF
    set -x
    run_meson setup . "$SRCROOT" --cross-file "$crossfile"
    { set +x; } 2>/dev/null
    config=$(run_meson introspect --buildoptions)
fi

# Reconfigure only if values differ from current Meson state.
buildtype=debug
b_ndebug=false
if [[ $CONFIGURATION == Release ]]; then
    buildtype=debugoptimized
fi
configurable_vars=(buildtype log b_ndebug log_handler kernel kconfig)
if [[ -n "${ENABLE_ADDRESS_SANITIZER:-}" ]]; then
    b_sanitize=address
    configurable_vars+=(b_sanitize)
fi
log=${ISH_LOG:-}
log_handler=${ISH_LOGGER:-dprintf}
kernel=ish
if [[ -n "${ISH_KERNEL:-}" ]]; then
    kernel=$ISH_KERNEL
fi
kconfig=""
for var in "${configurable_vars[@]}"; do
    old_value=$(run_clean python3 -c "import sys, json; v = next(x['value'] for x in json.load(sys.stdin) if x['name'] == '$var'); print(str(v).lower() if isinstance(v, bool) else ','.join(v) if isinstance(v, list) else v)" <<< "$config")
    new_value=${!var}
    if [[ $old_value != $new_value ]]; then
        set -x; run_meson configure "-D$var=$new_value"
    fi
done
