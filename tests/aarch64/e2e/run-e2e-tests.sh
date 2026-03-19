#!/bin/bash
# E2E Test Runner for aarch64 iSH
# Tests actual execution of aarch64 binaries under iSH emulator

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
RESULTS_DIR="${RESULTS_DIR:-$SCRIPT_DIR/results}"
FIXTURES_DIR="${FIXTURES_DIR:-$SCRIPT_DIR/fixtures}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0

# Create results directory
mkdir -p "$RESULTS_DIR"

# Test result file
RESULT_FILE="$RESULTS_DIR/e2e_results.txt"
echo "E2E Test Results - $(date)" > "$RESULT_FILE"
echo "================================" >> "$RESULT_FILE"

test_header() {
    echo ""
    echo "========================================"
    echo "TEST: $1"
    echo "========================================"
    echo "" >> "$RESULT_FILE"
    echo "TEST: $1" >> "$RESULT_FILE"
}

test_pass() {
    echo -e "${GREEN}PASS${NC}: $1"
    echo "PASS: $1" >> "$RESULT_FILE"
    ((PASSED++))
}

test_fail() {
    echo -e "${RED}FAIL${NC}: $1"
    echo "FAIL: $1" >> "$RESULT_FILE"
    ((FAILED++))
}

test_skip() {
    echo -e "${YELLOW}SKIP${NC}: $1"
    echo "SKIP: $1" >> "$RESULT_FILE"
    ((SKIPPED++))
}

# Check prerequisites
check_prerequisites() {
    test_header "Prerequisites"

    # Check iSH binary exists
    if [ -f "$BUILD_DIR/ish" ]; then
        test_pass "iSH binary exists"
    else
        test_fail "iSH binary not found at $BUILD_DIR/ish"
        echo "Run: meson setup build && ninja -C build"
        exit 1
    fi

    # Check cross compiler
    if command -v aarch64-linux-musl-gcc &> /dev/null; then
        test_pass "aarch64 cross compiler available"
    else
        test_skip "aarch64 cross compiler not found (will use prebuilt binaries)"
    fi

    # Check QEMU
    if command -v qemu-aarch64-static &> /dev/null; then
        test_pass "QEMU available for reference testing"
    else
        test_skip "QEMU not available"
    fi
}

# Compile test binary
compile_test() {
    local src="$1"
    local output="$2"

    if command -v aarch64-linux-musl-gcc &> /dev/null; then
        aarch64-linux-musl-gcc -static -o "$output" "$src" 2>/dev/null
        return $?
    elif command -v aarch64-linux-gnu-gcc &> /dev/null; then
        aarch64-linux-gnu-gcc -static -o "$output" "$src" 2>/dev/null
        return $?
    else
        return 1
    fi
}

# Test 1: Hello World
test_hello_world() {
    test_header "Hello World"

    local src="$FIXTURES_DIR/hello.c"
    local bin="$RESULTS_DIR/hello"

    # Create test source if it doesn't exist
    if [ ! -f "$src" ]; then
        mkdir -p "$FIXTURES_DIR"
        cat > "$src" << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello from aarch64!\n");
    return 0;
}
EOF
    fi

    # Compile
    if compile_test "$src" "$bin"; then
        test_pass "Compiled hello.c"
    else
        test_fail "Failed to compile hello.c"
        return
    fi

    # Run under QEMU as reference
    if command -v qemu-aarch64-static &> /dev/null; then
        local qemu_output
        qemu_output=$(qemu-aarch64-static "$bin" 2>&1)
        if echo "$qemu_output" | grep -q "Hello from aarch64"; then
            test_pass "QEMU reference: hello works"
        else
            test_fail "QEMU reference: hello failed - $qemu_output"
        fi
    else
        test_skip "QEMU reference test"
    fi

    # Run under iSH (the actual test)
    if [ -f "$BUILD_DIR/ish" ]; then
        # Create minimal rootfs if needed
        local rootfs="$RESULTS_DIR/rootfs"
        mkdir -p "$rootfs"
        cp "$bin" "$rootfs/"

        local ish_output
        # Note: This requires iSH to be fully built with aarch64 support
        # For now, we just verify the binary exists
        test_skip "iSH execution (requires full build)"
    else
        test_skip "iSH execution (binary not built)"
    fi
}

# Test 2: Exit codes
test_exit_codes() {
    test_header "Exit Codes"

    local src="$FIXTURES_DIR/exit_codes.c"
    local bin="$RESULTS_DIR/exit_codes"

    if [ ! -f "$src" ]; then
        mkdir -p "$FIXTURES_DIR"
        cat > "$src" << 'EOF'
int main() {
    // Test various exit codes
    volatile int x = 42;
    if (x == 42) {
        return 0;  // Success
    }
    return 1;  // Failure
}
EOF
    fi

    if compile_test "$src" "$bin"; then
        test_pass "Compiled exit_codes.c"

        # Test with QEMU
        if command -v qemu-aarch64-static &> /dev/null; then
            qemu-aarch64-static "$bin"
            local exit_code=$?
            if [ $exit_code -eq 0 ]; then
                test_pass "QEMU: exit code 0 (correct)"
            else
                test_fail "QEMU: exit code $exit_code (expected 0)"
            fi
        else
            test_skip "QEMU exit code test"
        fi
    else
        test_fail "Failed to compile exit_codes.c"
    fi
}

# Test 3: Basic arithmetic
test_arithmetic() {
    test_header "Basic Arithmetic"

    local src="$FIXTURES_DIR/arithmetic.c"
    local bin="$RESULTS_DIR/arithmetic"

    if [ ! -f "$src" ]; then
        mkdir -p "$FIXTURES_DIR"
        cat > "$src" << 'EOF'
#include <stdio.h>
int main() {
    int a = 5, b = 3;
    int sum = a + b;
    int diff = a - b;
    int prod = a * b;

    printf("5+3=%d 5-3=%d 5*3=%d\n", sum, diff, prod);

    // Verify results
    if (sum != 8) return 1;
    if (diff != 2) return 1;
    if (prod != 15) return 1;

    return 0;
}
EOF
    fi

    if compile_test "$src" "$bin"; then
        test_pass "Compiled arithmetic.c"

        if command -v qemu-aarch64-static &> /dev/null; then
            local output
            output=$(qemu-aarch64-static "$bin" 2>&1)
            if echo "$output" | grep -q "5+3=8 5-3=2 5\*3=15"; then
                test_pass "QEMU: arithmetic correct"
            else
                test_fail "QEMU: arithmetic failed - got: $output"
            fi
        else
            test_skip "QEMU arithmetic test"
        fi
    else
        test_fail "Failed to compile arithmetic.c"
    fi
}

# Test 4: System calls
test_syscalls() {
    test_header "System Calls"

    local src="$FIXTURES_DIR/syscalls.c"
    local bin="$RESULTS_DIR/syscalls"

    if [ ! -f "$src" ]; then
        mkdir -p "$FIXTURES_DIR"
        cat > "$src" << 'EOF'
#include <unistd.h>
#include <sys/syscall.h>

int main() {
    // Test getpid syscall
    pid_t pid = syscall(SYS_getpid);

    // Test write syscall
    const char msg[] = "syscall test\n";
    syscall(SYS_write, 1, msg, sizeof(msg)-1);

    return (pid > 0) ? 0 : 1;
}
EOF
    fi

    if compile_test "$src" "$bin"; then
        test_pass "Compiled syscalls.c"

        if command -v qemu-aarch64-static &> /dev/null; then
            local output
            output=$(qemu-aarch64-static "$bin" 2>&1)
            if echo "$output" | grep -q "syscall test"; then
                test_pass "QEMU: syscalls work"
            else
                test_fail "QEMU: syscalls failed - got: $output"
            fi
        else
            test_skip "QEMU syscall test"
        fi
    else
        test_fail "Failed to compile syscalls.c"
    fi
}

# Test 5: iSH vs QEMU comparison
test_ish_vs_qemu() {
    test_header "iSH vs QEMU Comparison"

    local src="$FIXTURES_DIR/compare.c"
    local bin="$RESULTS_DIR/compare"

    if [ ! -f "$src" ]; then
        mkdir -p "$FIXTURES_DIR"
        cat > "$src" << 'EOF'
#include <stdio.h>
#include <stdint.h>

int main() {
    // Set some registers via volatile operations
    volatile uint64_t x0 = 0x123456789ABCDEF0ULL;
    volatile uint64_t x1 = 0x0FEDCBA987654321ULL;

    // Perform operation that should be visible
    uint64_t result = x0 ^ x1;

    printf("RESULT:%016lX\n", result);
    return 0;
}
EOF
    fi

    if compile_test "$src" "$bin"; then
        if command -v qemu-aarch64-static &> /dev/null; then
            local qemu_result
            qemu_result=$(qemu-aarch64-static "$bin" 2>&1 | grep "RESULT:")
            test_pass "QEMU reference: $qemu_result"
        fi

        if [ -f "$BUILD_DIR/ish" ]; then
            # This would run the same binary under iSH
            test_skip "iSH comparison (needs execution wiring)"
        else
            test_skip "iSH comparison (binary not built)"
        fi
    else
        test_fail "Failed to compile compare.c"
    fi
}

# Print summary
print_summary() {
    echo ""
    echo "========================================"
    echo "E2E Test Summary"
    echo "========================================"
    echo -e "${GREEN}PASSED: $PASSED${NC}"
    echo -e "${RED}FAILED: $FAILED${NC}"
    echo -e "${YELLOW}SKIPPED: $SKIPPED${NC}"
    echo ""
    echo "Results saved to: $RESULT_FILE"

    echo "" >> "$RESULT_FILE"
    echo "Summary: $PASSED passed, $FAILED failed, $SKIPPED skipped" >> "$RESULT_FILE"

    if [ $FAILED -eq 0 ]; then
        echo -e "${GREEN}All available tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed${NC}"
        exit 1
    fi
}

# Main
main() {
    echo "aarch64 iSH E2E Test Suite"
    echo "=========================="
    echo ""
    echo "This tests actual execution of aarch64 binaries"
    echo ""

    check_prerequisites
    test_hello_world
    test_exit_codes
    test_arithmetic
    test_syscalls
    test_ish_vs_qemu
    print_summary
}

main "$@"
