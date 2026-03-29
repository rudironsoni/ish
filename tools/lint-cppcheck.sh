#!/bin/bash
# Run cppcheck static analysis

echo "=== Running cppcheck ==="

cppcheck \
    --enable=all \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=unmatchedSuppression \
    --inline-suppr \
    --std=c11 \
    -I . \
    -I trace \
    --quiet \
    emu kernel fs tcti util 2>&1 | head -50 || true

echo "✓ cppcheck completed"
