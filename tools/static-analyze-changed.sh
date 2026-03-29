#!/bin/bash
# Run Clang Static Analyzer on changed files only (for pre-commit)
# Usage: static-analyze-changed.sh file1.c file2.c ...

set -e

if [ $# -eq 0 ]; then
    echo "No files to analyze"
    exit 0
fi

# Filter for C files only
C_FILES=()
for f in "$@"; do
    if [[ "$f" =~ \.(c|h)$ ]]; then
        C_FILES+=("$f")
    fi
done

if [ ${#C_FILES[@]} -eq 0 ]; then
    echo "No C files to analyze"
    exit 0
fi

echo "Running Clang Static Analyzer on ${#C_FILES[@]} file(s)..."

# Check if scan-build is available
if ! command -v scan-build &> /dev/null; then
    echo "Warning: scan-build not found, skipping static analysis"
    exit 0
fi

# Create temporary build directory
BUILD_DIR=$(mktemp -d)
trap "rm -rf $BUILD_DIR" EXIT

# Run clang --analyze directly on changed files (faster than full scan-build)
ERRORS=0
for file in "${C_FILES[@]}"; do
    echo "Analyzing: $file"
    if ! clang --analyze \
        -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-disable-checker=deadcode.DeadStores \
        -I. -Itrace \
        -DARCH_AARCH64=1 \
        -DENGINE_TCTI=1 \
        "$file" 2>&1; then
        ERRORS=$((ERRORS + 1))
    fi
done

if [ $ERRORS -gt 0 ]; then
    echo "Clang Static Analyzer found $ERRORS issue(s)"
    exit 1
fi

echo "No issues found by Clang Static Analyzer"
exit 0
