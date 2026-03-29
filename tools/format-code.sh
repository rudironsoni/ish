#!/bin/bash
# Fix code formatting automatically

echo "=== Fixing code formatting with clang-format ==="

find . -name "*.c" -o -name "*.h" | grep -v "^./deps/" | grep -v "^./build/" | grep -v "^./builddir/" | grep -v "^./\.cache/" | xargs clang-format -i

echo "✓ Formatting applied to all files"
