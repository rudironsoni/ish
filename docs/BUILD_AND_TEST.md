# Build and Test Instructions

**iOS-Only Test Policy**: All tests MUST execute against iOS Simulator or iOS device only.

## Build

### Generate Project

```bash
xcodegen generate
```

### iOS App (Xcode)

```bash
xcodebuild build -project IXLand.xcodeproj -scheme IXLand -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
```

### Runtime Library Only

```bash
xcodebuild build -project IXLand.xcodeproj -scheme IXLandLinuxRuntime -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
```

## Test

### Unit Tests

```bash
xcodebuild test -project IXLand.xcodeproj -scheme IXLandTerminalUnitTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
```

### Runtime Tests

```bash
xcodebuild test -project IXLand.xcodeproj -scheme IXLandLinuxRuntimeUnitTests -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 17'
```

## Project Structure

- `Sources/IXLandLinuxRuntime/` - Linux emulation runtime (C)
- `Sources/IXLandTerminal/` - iOS terminal app (Obj-C/Swift)
- `Packages/IXLandInstrumentation/` - Instrumentation package (C/Obj-C++)
- `Tests/` - Test suites
- `tools/` - Build utilities
