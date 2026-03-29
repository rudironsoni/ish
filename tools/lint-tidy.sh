#!/bin/bash
# Run clang-tidy (requires compile_commands.json)

echo "=== Running clang-tidy ==="

if [ ! -f "builddir/compile_commands.json" ] && [ ! -f "build/compile_commands.json" ]; then
    echo "Warning: compile_commands.json not found. Run 'meson setup builddir' first."
    exit 1
fi

TIDY_ARGS=""
if [ -f "builddir/compile_commands.json" ]; then
    TIDY_ARGS="-p builddir"
else
    TIDY_ARGS="-p build"
fi

find emu kernel fs tcti util -name "*.c" | grep -v "\.S$" | while read -r file; do
    echo "Checking: $file"
    clang-tidy $TIDY_ARGS "$file" 2>/dev/null || true
done

echo "✓ clang-tidy completed"
