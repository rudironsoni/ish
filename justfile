# iSH Justfile - Task runner for development workflows
# Install just: https://github.com/casey/just

# Default recipe - show available commands
default:
    @just --list

# Build the project
build:
    meson setup builddir 2>/dev/null || true
    ninja -C builddir

# Run all tests
test:
    meson test -C builddir

# Run specific test suite
test-suite SUITE:
    meson test -C builddir --suite {{SUITE}}

# Clean build artifacts
clean:
    rm -rf builddir build

# === Linting Commands ===

# Run all linting checks
lint: lint-format lint-cppcheck lint-tidy
    @echo "=== Lint Summary ==="
    @echo "✓ All checks completed"

# Check code formatting (no changes)
lint-format:
    @echo "=== Checking code formatting with clang-format ==="
    @bash -c 'set -e; \
        FILES=$$(find . -name "*.c" -o -name "*.h" | grep -v "^./deps/" | grep -v "^./build/" | grep -v "^./builddir/" | grep -v "^./\.cache/" | sort); \
        ERRORS=0; \
        for file in $$FILES; do \
            if ! clang-format --dry-run --Werror "$$file" 2>/dev/null; then \
                echo "  Format error: $$file"; \
                ERRORS=$$((ERRORS + 1)); \
            fi; \
        done; \
        if [ $$ERRORS -gt 0 ]; then \
            echo ""; \
            echo "$$ERRORS file(s) need formatting. Run: just format"; \
            exit 1; \
        fi; \
        echo "✓ All files are properly formatted"'

# Fix code formatting automatically
format:
    @echo "=== Fixing code formatting with clang-format ==="
    find . -name "*.c" -o -name "*.h" | grep -v "^./deps/" | grep -v "^./build/" | grep -v "^./builddir/" | grep -v "^./\.cache/" | xargs clang-format -i
    @echo "✓ Formatting applied to all files"

# Run cppcheck static analysis
lint-cppcheck:
    @echo "=== Running cppcheck ==="
    @cppcheck \
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
    @echo "✓ cppcheck completed"

# Run clang-tidy (requires compile_commands.json)
lint-tidy:
    @echo "=== Running clang-tidy ==="
    @bash -c 'if [ ! -f "builddir/compile_commands.json" ] && [ ! -f "build/compile_commands.json" ]; then \
        echo "Warning: compile_commands.json not found. Run just build first."; \
        exit 1; \
    fi; \
    TIDY_ARGS=""; \
    if [ -f "builddir/compile_commands.json" ]; then \
        TIDY_ARGS="-p builddir"; \
    else \
        TIDY_ARGS="-p build"; \
    fi; \
    find emu kernel fs tcti util -name "*.c" | grep -v "\.S$$" | while read -r file; do \
        echo "Checking: $$file"; \
        clang-tidy $$TIDY_ARGS "$$file" 2>/dev/null || true; \
    done; \
    echo "✓ clang-tidy completed"'

# === Pre-commit Commands ===

# Run all pre-commit hooks
pre-commit:
    pre-commit run --all-files

# Install pre-commit hooks
install-hooks:
    pip install pre-commit
    pre-commit install
    @echo "✓ Pre-commit hooks installed"

# === Development Commands ===

# Quick check - build and test
check: build test
    @echo "✓ Build and tests passed"

# Run decoder tests only
test-decoder:
    meson test -C builddir decoder

# Run generator tests only
test-generator:
    meson test -C builddir generator

# Run case tests only
test-cases:
    meson test -C builddir --suite cases

# Validate generated files are not in source tree
validate-generated:
    @./tools/validate_generated.sh

# === Utility Commands ===

# Show project info
info:
    @echo "iSH - Linux shell emulator for iOS"
    @echo ""
    @echo "Build system: Meson + Ninja"
    @echo "Architecture: AArch64 (TCTI execution engine)"
    @echo ""
    @echo "Key directories:"
    @echo "  emu/      - CPU emulator (AArch64 decode, TLB, memory)"
    @echo "  kernel/   - Syscall implementation"
    @echo "  fs/       - Filesystem layer"
    @echo "  tcti/     - Threaded Code Translation and Interpretation"
    @echo "  tests/    - Unit and integration tests"

# Count lines of code (excluding deps)
loc:
    @find . -name "*.c" -o -name "*.h" | grep -v "^./deps/" | xargs wc -l | tail -1

# Find TODO/FIXME comments in source
todos:
    @grep -r "TODO\|FIXME\|XXX" --include="*.c" --include="*.h" emu kernel fs tcti util | grep -v "^Binary" | head -30 || echo "No TODOs found"
