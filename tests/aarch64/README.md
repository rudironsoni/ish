# aarch64 Test Suite

This directory contains unit tests for the aarch64 emulation layer.

## Tests

### decoder_test.c
Tests the aarch64 instruction decoder with:
- Data processing instructions (MOV, ADD, SUB, etc.)
- Branch instructions (B, BL, CBZ, RET)
- Load/Store instructions
- System instructions (SVC, barriers)
- Fuzz testing for crash resistance

### gadget_test.c
Tests the TCTI gadget implementation with:
- CPU state structure validation
- Register access patterns
- Flag calculation verification
- Gadget table lookups

## Building

```bash
# Build all tests
meson build -Darch=aarch64
ninja -C build test

# Build specific test
gcc -I../../.. tests/aarch64/decoder_test.c -o decoder_test

# Run tests
./decoder_test
./gadget_test
```

## Adding Tests

To add a new test:

1. Create test function:
```c
TEST(my_feature) {
    ASSERT_EQ(expected, actual);
    ASSERT(some_condition);
}
```

2. Register in main():
```c
RUN_TEST(my_feature);
```

## Integration with Main Test Suite

These tests are designed to integrate with meson's test runner:
```bash
meson test -v --suite aarch64
```
