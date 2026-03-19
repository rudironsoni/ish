#!/bin/bash
# Compare iSH execution against QEMU reference
# Usage: ./qemu-vs-ish.sh <test_binary>

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${1:-}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/../../../build}"

if [ -z "$BINARY" ] || [ ! -f "$BINARY" ]; then
    echo "Usage: $0 <aarch64_test_binary>"
    echo ""
    echo "Example:"
    echo "  $0 ./binaries/hello.bin"
    exit 1
fi

echo "================================"
echo "QEMU vs iSH Comparison"
echo "================================"
echo ""
echo "Test binary: $BINARY"
echo ""

# Check prerequisites
if ! command -v qemu-aarch64-static &> /dev/null; then
    echo "ERROR: qemu-aarch64-static not found"
    echo "Install: apt-get install qemu-user-static"
    exit 1
fi

if [ ! -f "$BUILD_DIR/ish" ]; then
    echo "ERROR: iSH binary not found at $BUILD_DIR/ish"
    exit 1
fi

# Create temporary directories
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

QEMU_OUT="$TMPDIR/qemu.out"
QEMU_ERR="$TMPDIR/qemu.err"
ISHI_OUT="$TMPDIR/ish.out"
ISHI_ERR="$TMPDIR/ish.err"

echo "1. Running under QEMU (reference)..."
set +e
qemu-aarch64-static "$BINARY" > "$QEMU_OUT" 2> "$QEMU_ERR"
QEMU_EXIT=$?
set -e

echo "   Exit code: $QEMU_EXIT"
echo "   Stdout:"
cat "$QEMU_OUT" | sed 's/^/     /'
if [ -s "$QEMU_ERR" ]; then
    echo "   Stderr:"
    cat "$QEMU_ERR" | sed 's/^/     /'
fi
echo ""

# For iSH, we need a rootfs with the binary
echo "2. Preparing iSH environment..."
ROOTFS="$TMPDIR/rootfs"
mkdir -p "$ROOTFS"
cp "$BINARY" "$ROOTFS/test.bin"

echo "   Rootfs: $ROOTFS"
echo ""

echo "3. Running under iSH..."
set +e
$BUILD_DIR/ish -f "$ROOTFS" /test.bin > "$ISHI_OUT" 2> "$ISHI_ERR"
ISHI_EXIT=$?
set -e

echo "   Exit code: $ISHI_EXIT"
echo "   Stdout:"
cat "$ISHI_OUT" | sed 's/^/     /'
if [ -s "$ISHI_ERR" ]; then
    echo "   Stderr:"
    cat "$ISHI_ERR" | sed 's/^/     /'
fi
echo ""

# Compare results
echo "================================"
echo "Comparison Results"
echo "================================"

PASS=0
FAIL=0

# Compare exit codes
if [ "$QEMU_EXIT" -eq "$ISHI_EXIT" ]; then
    echo "✅ Exit codes match: $QEMU_EXIT"
    ((PASS++))
else
    echo "❌ Exit codes differ: QEMU=$QEMU_EXIT, iSH=$ISHI_EXIT"
    ((FAIL++))
fi

# Compare stdout
if diff -q "$QEMU_OUT" "$ISHI_OUT" > /dev/null 2>&1; then
    echo "✅ Stdout matches"
    ((PASS++))
else
    echo "❌ Stdout differs:"
    echo "   QEMU: $(cat $QEMU_OUT | head -c 50)"
    echo "   iSH:  $(cat $ISHI_OUT | head -c 50)"
    ((FAIL++))
fi

# Compare stderr (if any)
if [ -s "$QEMU_ERR" ] || [ -s "$ISHI_ERR" ]; then
    if diff -q "$QEMU_ERR" "$ISHI_ERR" > /dev/null 2>&1; then
        echo "✅ Stderr matches"
        ((PASS++))
    else
        echo "⚠️  Stderr differs (may be expected)"
    fi
fi

echo ""
echo "================================"
echo "Summary: $PASS passed, $FAIL failed"
echo "================================"

if [ $FAIL -eq 0 ]; then
    exit 0
else
    exit 1
fi
