#!/bin/bash
# Run all code quality checks

echo "=== Running all code quality checks ==="

meson compile lint
meson compile dead-code

echo "=== All code quality checks passed ==="
