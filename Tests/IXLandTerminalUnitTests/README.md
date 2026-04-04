# IXLandTerminal Unit Tests

## Running Tests

```bash
# Command line
xcodebuild test -project IXLand.xcodeproj -scheme IXLandTerminalUnitTests -destination 'platform=iOS Simulator,name=iPhone 17'

# Or in Xcode
Cmd+U to run all tests
```

## Test Coverage

| Component | Tests | Type |
|-----------|-------|------|
| AArch64 Decoder | 39 test cases | Unit |
| Instruction decoding | ADD, SUB, MOVZ, B, BL, RET, NOP, CBZ, LDR, STR, SVC, etc. | Unit |
| Register handling | All 31 X registers | Unit |
| Category detection | All instruction categories | Unit |
| Edge cases | Zero instruction, all ones, all registers | Unit |
| Fuzz testing | 10K random, known patterns | Unit |
| Performance | Decode 10000 instructions | Unit |
