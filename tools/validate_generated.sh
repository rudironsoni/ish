#!/bin/bash
# Validation script for generated TCTI gadget files
# Detects stale outputs or manual edits

set -e

GENERATOR="tcti/aarch64/tcti-gadget-gen.py"
BUILD_DIR="${1:-build}"

echo "=== TCTI Generated Files Validation ==="
echo "Generator: $GENERATOR"
echo "Build directory: $BUILD_DIR"
echo ""

# Check if generated files exist in build directory
if [ ! -f "$BUILD_DIR/gadgets_tcti.h" ] || [ ! -f "$BUILD_DIR/gadgets_tcti_impl.c" ]; then
    echo "ERROR: Generated files not found in build directory"
    echo "Run: meson setup $BUILD_DIR && ninja -C $BUILD_DIR"
    exit 1
fi

# Extract generator hash from generated files
GENERATED_HASH=$(grep "Generator Hash:" "$BUILD_DIR/gadgets_tcti.h" | head -1 | sed 's/.*Generator Hash: \([a-f0-9]*\).*/\1/')

echo "Generated file hash: $GENERATED_HASH"

# Check if generator exists
if [ ! -f "$GENERATOR" ]; then
    echo "ERROR: Generator not found at $GENERATOR"
    exit 1
fi

# Compute current generator hash
CURRENT_HASH=$(python3 -c "import hashlib; print(hashlib.sha256(open('$GENERATOR', 'rb').read()).hexdigest()[:16])")

echo "Current generator hash: $CURRENT_HASH"
echo ""

if [ "$GENERATED_HASH" != "$CURRENT_HASH" ]; then
    echo "ERROR: Generated files are STALE"
    echo "  Generated with hash: $GENERATED_HASH"
    echo "  Current generator hash: $CURRENT_HASH"
    echo ""
    echo "Regenerate with: rm -rf $BUILD_DIR && meson setup $BUILD_DIR && ninja -C $BUILD_DIR"
    exit 1
fi

# Check for manual edits in generated files
# (Look for lines that don't match expected patterns)
if grep -v "^ \*" "$BUILD_DIR/gadgets_tcti.h" | grep -v "^/\*" | grep -v "^#" | grep -v "^$" | grep -v "^extern" | grep -v "^typedef" | grep -v "^struct" | grep -v "^#define" | grep -v "^$" | head -5; then
    echo "WARNING: Unexpected content in generated header (possible manual edit)"
fi

echo "✓ Generated files are valid and up-to-date"
echo "✓ Generator hash matches: $CURRENT_HASH"
exit 0
