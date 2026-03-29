#!/bin/bash
# iSH Mission Initialization Script
# This script is run at the start of each worker session

set -e

echo "=== iSH Mission Initialization ==="

# Check for Meson build directory
if [ ! -d "builddir" ]; then
    echo "Setting up Meson build directory..."
    meson setup builddir
fi

# Verify build system works
echo "Verifying build system..."
meson compile -C builddir --dry-run 2>/dev/null || true

echo "Initialization complete."
