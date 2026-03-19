# E2E Tests for aarch64 iSH

## Overview

These are **real execution tests** that verify aarch64 binaries actually run under iSH and produce correct output.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Test Framework                                                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │  C Source       │  │  Cross Compile  │  │  aarch64 Binary │ │
│  │  (test.c)       │──▶│  (musl/glibc)   │──▶│  (static)       │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
│         │                                            │            │
│         ▼                                            ▼            │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                     Comparison Engine                      │ │
│  │  ┌────────────────┐         ┌────────────────┐            │ │
│  │  │  QEMU (ref)    │         │  iSH (target)  │            │ │
│  │  │  Run binary    │         │  Run binary    │            │ │
│  │  │  Capture out   │         │  Capture out   │            │ │
│  │  └───────┬────────┘         └───────┬────────┘            │ │
│  │          │                          │                     │ │
│  │          ▼                          ▼                     │ │
│  │     ┌──────────────────────────────────────┐            │ │
│  │     │  Compare: exit codes, stdout, regs   │            │ │
│  │     │  Assert: QEMU == iSH               │            │ │
│  │     └──────────────────────────────────────┘            │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## Quick Start

### Using Docker (Recommended)

```bash
# Build the test environment
docker-compose build

# Run all E2E tests
docker-compose run --rm builder

# Run just QEMU reference tests
docker-compose run --rm qemu-test

# Build and test iSH (full integration)
docker-compose run --rm ish-test
```

### Using Local Environment

```bash
# Prerequisites
sudo apt-get install gcc-aarch64-linux-gnu qemu-user-static

# Download musl cross compiler
wget https://musl.cc/aarch64-linux-musl-cross.tgz
tar xzf aarch64-linux-musl-cross.tgz
export PATH="$(pwd)/aarch64-linux-musl-cross/bin:$PATH"

# Run tests
./run-e2e-tests.sh
```

## Test Coverage

| Test | Description | QEMU | iSH |
|------|-------------|------|-----|
| hello_world | Basic printf | ✅ | ⏳ |
| exit_codes | Return values 0, 1, etc. | ✅ | ⏳ |
| arithmetic | ADD, SUB, MUL operations | ✅ | ⏳ |
| syscalls | write, getpid, exit | ✅ | ⏳ |
| register_ops | X0-X30 operations | ✅ | ⏳ |
| memory_ops | Load/store with addresses | ✅ | ⏳ |
| branching | B, BL, RET, CBZ | ✅ | ⏳ |
| flags | NZCV flag setting | ✅ | ⏳ |

Legend:
- ✅ Implemented and working
- ⏳ Waiting for iSH execution integration
- ❌ Not yet implemented

## Test File Structure

```
e2e/
├── Dockerfile                    # Cross-compilation environment
├── docker-compose.yml            # Multi-service orchestration
├── run-e2e-tests.sh             # Main test runner
├── README.md                      # This file
├── fixtures/                      # Test C source files
│   ├── hello.c
│   ├── exit_codes.c
│   ├── arithmetic.c
│   ├── syscalls.c
│   └── compare.c
├── binaries/                      # Compiled test binaries
│   └── (generated)
└── results/                       # Test output & logs
    ├── e2e_results.txt
    └── (test artifacts)
```

## Writing New E2E Tests

### Template

```c
// fixtures/my_test.c
#include <stdio.h>

int main() {
    // Test code here
    printf("TEST_OUTPUT:42\n");
    return 0;
}
```

### Test Function

```bash
# Add to run-e2e-tests.sh
test_my_feature() {
    test_header "My Feature"

    local src="$FIXTURES_DIR/my_test.c"
    local bin="$RESULTS_DIR/my_test"

    # Create source if needed
    if [ ! -f "$src" ]; then
        cat > "$src" << 'EOF'
#include <stdio.h>
int main() {
    printf("TEST_OUTPUT:42\n");
    return 0;
}
EOF
    fi

    # Compile
    if compile_test "$src" "$bin"; then
        test_pass "Compiled my_test.c"
    else
        test_fail "Compilation failed"
        return
    fi

    # Run under QEMU
    if command -v qemu-aarch64-static &> /dev/null; then
        local output
        output=$(qemu-aarch64-static "$bin" 2>&1)
        if echo "$output" | grep -q "TEST_OUTPUT:42"; then
            test_pass "QEMU: output correct"
        else
            test_fail "QEMU: wrong output: $output"
        fi
    fi

    # Run under iSH (when available)
    if [ -f "$BUILD_DIR/ish" ]; then
        # Create rootfs, run binary, compare output
        test_skip "iSH execution"
    fi
}
```

## Comparison with Unit Tests

| Aspect | Unit Tests | E2E Tests |
|--------|-----------|-----------|
| **What they test** | Decoder correctness | Full execution |
| **Input** | Raw instruction bytes | Compiled binaries |
| **Verification** | Field values | Actual behavior |
| **Speed** | Fast (μs) | Slower (ms) |
| **Scope** | Instruction in isolation | Multi-instruction sequences |
| **Trustworthiness** | Medium | **High** |

## CI/CD Integration

```yaml
# .github/workflows/e2e-tests.yml
name: E2E Tests

on: [push, pull_request]

jobs:
  e2e:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2

      - name: Set up Docker
        uses: docker/setup-buildx-action@v1

      - name: Build test environment
        run: docker-compose build

      - name: Run E2E tests
        run: docker-compose run --rm builder

      - name: Upload results
        uses: actions/upload-artifact@v2
        with:
          name: e2e-results
          path: tests/aarch64/e2e/results/
```

## Debugging Failed Tests

### Check QEMU Output

```bash
qemu-aarch64-static -strace ./binaries/test.bin
```

### Compare Register States

```bash
# Run with register dump
qemu-aarch64-static -d cpu,exec ./binaries/test.bin 2>&1 | head -50
```

### Inspect Binary

```bash
aarch64-linux-musl-objdump -d ./binaries/test.bin | head -30
```

## Roadmap

- [x] Test framework infrastructure
- [x] QEMU reference validation
- [ ] iSH execution integration
- [ ] Register state comparison
- [ ] Memory access verification
- [ ] Signal handling tests
- [ ] Multi-threading tests
- [ ] Performance benchmarks
