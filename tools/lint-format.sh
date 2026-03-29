#!/bin/bash
# Check code formatting with clang-format

set -e

echo "=== Checking code formatting with clang-format ==="

FILES=$(find . -name "*.c" -o -name "*.h" | grep -v "^./deps/" | grep -v "^./build/" | grep -v "^./builddir/" | grep -v "^./\.cache/" | sort)
ERRORS=0

for file in $FILES; do
    if ! clang-format --dry-run --Werror "$file" 2>/dev/null; then
        echo "  Format error: $file"
        ERRORS=$((ERRORS + 1))
    fi
done

if [ $ERRORS -gt 0 ]; then
    echo ""
    echo "$ERRORS file(s) need formatting. Run: meson compile format"
    exit 1
fi

echo "✓ All files are properly formatted"
