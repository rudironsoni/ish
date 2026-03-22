#!/bin/bash
# Clean build script for iSH - ensures no stale artifacts

set -e

echo "=== Cleaning build artifacts ==="
# Remove build directory
rm -rf build

# Remove any stale object files in source tree
find . -name "*.o" -type f -delete 2>/dev/null || true
find . -name "*.a" -type f -delete 2>/dev/null || true

# Remove asbestos artifacts if any still exist
find . -name "*asbestos*" -type f -delete 2>/dev/null || true

echo "=== Verifying TCTI files exist ==="
for file in tcti/aarch64/gen.c tcti/aarch64/gadgets_memory.c tcti/aarch64/tcti_entry.S; do
    if [ ! -f "$file" ]; then
        echo "ERROR: Missing $file"
        exit 1
    fi
    echo "  ✓ $file"
done

echo "=== Configuring build ==="
meson setup build "$@"

echo "=== Building ==="
ninja -C build

echo "=== Running tests ==="
ninja -C build test

echo "=== Build complete ==="
echo "Binary: build/ish"
