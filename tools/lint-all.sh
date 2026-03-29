#!/bin/bash
# Run all linting checks

echo "=== Running all lint checks ==="

meson compile lint-format
meson compile lint-cppcheck
meson compile lint-tidy

echo "=== Lint Summary ==="
echo "✓ All checks completed"
