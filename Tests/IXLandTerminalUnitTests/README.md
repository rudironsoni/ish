# iSH Xcode Tests for aarch64

## Test Files Created

### Unit Tests (app/Tests/)
- **Aarch64DecoderTests.m** - XCTest unit tests for instruction decoder
  - Tests ADD, SUB, MOVZ, B, BL, RET, NOP, CBZ, LDR, STR, SVC decoding
  - Tests all 31 registers
  - Tests category detection
  - Performance benchmark (decode 10000 instructions)

### UI Tests (app/UITests/)
- **Aarch64EmulatorTests.m** - XCTest UI tests for full emulator
  - Tests shell execution in iOS app
  - Tests architecture detection (`uname -m`)
  - Tests arithmetic, file ops, pipes, environment variables
  - Tests signal handling and process creation

## Xcode Integration

To add these tests to the Xcode project:

### 1. Add Unit Test Target

In Xcode:
1. File → New → Target
2. Select "iOS Unit Testing Bundle"
3. Name: `iSH Aarch64 Tests`
4. Add files: `Aarch64DecoderTests.m`

### 2. Configure Build Settings

Add to Unit Test Target:
- **Header Search Paths**: `$(SRCROOT)/../../emu`, `$(SRCROOT)/../../emu/aarch64`
- **Source Files**: Add `decode.c` to compile sources

### 3. Add UI Test Target

1. File → New → Target
2. Select "iOS UI Testing Bundle"
3. Name: `iSH Aarch64 UI Tests`
4. Add files: `Aarch64EmulatorTests.m`

### 4. Run Tests

```bash
# Command line
xcodebuild test -project iSH.xcodeproj -scheme "iSH" -destination 'platform=iOS Simulator,name=iPhone 15'

# Or in Xcode
Cmd+U to run all tests
```

## Test Coverage

| Component | Tests | Type |
|-----------|-------|------|
| Decoder | 13 test cases | Unit |
| Instruction decoding | ADD, SUB, MOVZ, B, BL, etc. | Unit |
| Register handling | All 31 X registers | Unit |
| Category detection | All instruction categories | Unit |
| Shell execution | Basic commands | UI |
| File operations | read/write files | UI |
| Process management | fork/exec | UI |
| Environment | Variables, pipes | UI |

## CI/CD Integration

```yaml
# .github/workflows/ios-tests.yml
name: iOS Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v2

      - name: Run Tests
        run: |
          xcodebuild test \
            -project iSH.xcodeproj \
            -scheme iSH \
            -destination 'platform=iOS Simulator,name=iPhone 15' \
            | xcpretty
```

## Manual Testing

### In iOS Simulator:
1. Build and run iSH in Simulator
2. Type: `uname -m` → should show `aarch64`
3. Type: `echo $((5+3))` → should show `8`
4. Type: `ls /bin` → should list binaries

### On Device (TestFlight):
1. Deploy to physical iPhone/iPad
2. Verify same commands work
3. Test performance (time `ls -la /`)
