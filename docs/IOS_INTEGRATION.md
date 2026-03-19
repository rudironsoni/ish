# iOS Integration Status

## Current State

### The Gap

| Component | Linux/Meson | iOS/Xcode | Status |
|-----------|-------------|-------------|--------|
| Decoder | ✅ Working | ⚠️ Needs integration | Tested via Docker |
| TCTI Gadgets | ✅ C files | ❌ Not in Xcode | Xcode uses old assembly |
| Execution Engine | ✅ Working | ❌ Not integrated | Meson-only |
| iOS Simulator | N/A | ❌ Not tested | Blocked by Xcode gap |

### The Problem

**Xcode project uses OLD assembly gadgets:**
- Path: `asbestos/gadgets-aarch64/*.S` (assembly)
- Files: `bits.S`, `control.S`, `entry.S`, `math.S`, etc.

**New TCTI implementation is C-based:**
- Path: `asbestos/aarch64/*.c` (C with naked functions)
- Files: `gadgets_entry.c`, `gadgets_arith.c`, `gadgets_math.c`, etc.
- **NOT integrated into Xcode project**

## Impact

The iOS app currently uses:
1. **Old assembly gadgets** that don't support full aarch64 emulation
2. **No TCTI execution engine** for running aarch64 binaries
3. **Old decoder** (x86-focused)

The aarch64 migration work (TCTI gadgets, new decoder) is **Meson-only**.

## Testing Status

### What I've Tested (Linux/Docker)
- ✅ Decoder field extraction
- ✅ ARM DDI 0487 validation
- ✅ QEMU-based execution
- ❌ **NOT** iOS Simulator
- ❌ **NOT** iOS Device

### What Needs Testing (iOS)
- ⏳ iOS Simulator (requires Xcode update)
- ⏳ iOS Device (requires Xcode update + device)
- ⏳ UI Tests (requires working emulator)

## Solutions

### Option 1: Update Xcode Project (Recommended)

Add the new C files to Xcode:

```bash
# On macOS with Xcode installed
tools/update-xcode-aarch64.sh

# Then open Xcode and add files manually
open iSH.xcodeproj
```

**Steps:**
1. Open iSH.xcodeproj
2. Right-click `asbestos` in Navigator
3. Add Files → Select `asbestos/aarch64/`
4. Choose "Create groups"
5. Build and test

### Option 2: Meson for iOS

Build with Meson instead of Xcode:

```bash
# Install iOS SDK for Meson
export SDKROOT=$(xcrun --sdk iphoneos --show-sdk-path)
export CC="clang -arch arm64 -isysroot $SDKROOT"

meson setup build-ios --cross-file ios-cross-file.txt
ninja -C build-ios
```

**Challenge**: Meson doesn't easily create `.app` bundles for iOS.

### Option 3: Hybrid Build

Keep Xcode for UI, use Meson for emulator core:

```
iSH.xcodeproj
├── UI (Objective-C/Swift) ← Xcode
├── Platform (iOS-specific) ← Xcode
└── Emulator Core ← Meson static library
```

**Steps:**
1. Build emulator as static library with Meson
2. Link into Xcode project
3. Keep Xcode for UI/platform

## Immediate Testing Plan

Since Xcode integration is pending, here's the validation strategy:

### Phase 1: Linux/Docker (Current)
- ✅ Decoder tests (ARM reference)
- ✅ QEMU-based execution
- ✅ CI/CD with GitHub Actions

### Phase 2: iOS Simulator (Next)
Requires Xcode project update:
```bash
# After Xcode update
xcodebuild test -project iSH.xcodeproj -scheme iSH -destination 'platform=iOS Simulator,name=iPhone 15'
```

### Phase 3: Device Testing
Requires Phase 2 + developer account:
```bash
# Test on physical device
xcodebuild test -project iSH.xcodeproj -scheme iSH -destination 'platform=iOS,name=My iPhone'
```

## CI/CD Considerations

### Current (Linux-only)
- Cost: $0.04/build
- Tests: Decoder only
- Coverage: ~60%

### Full (Linux + macOS)
- Cost: $0.84/build
- Tests: Decoder + iOS Simulator
- Coverage: ~95%

### Recommendation
- **PRs**: Linux-only (fast, cheap)
- **Releases**: Linux + macOS (full validation)

## Blockers

| Blocker | Resolution | ETA |
|---------|-----------|-----|
| Xcode project update | Manual file add | 1 hour |
| iOS Simulator testing | Requires Mac | Immediate after update |
| Device testing | Requires dev account | After simulator works |

## Next Steps

1. **Immediate**: Document the Xcode gap (done)
2. **Short-term**: Update Xcode project on macOS
3. **Medium-term**: Add iOS Simulator to CI
4. **Long-term**: Device testing + App Store

## Verification Checklist

Before claiming "iOS ready":
- [ ] Xcode project includes `asbestos/aarch64/*.c`
- [ ] iOS Simulator builds successfully
- [ ] `uname -m` returns "aarch64" in Simulator
- [ ] `/bin/sh` runs without crashes
- [ ] UI Tests pass (type command, see output)
- [ ] Performance acceptable (< 5s for `ls -la /`)
- [ ] Device testing on physical iPhone/iPad

## Current Status

**Summary**: The aarch64 emulator is architecturally complete and tested on Linux, but **NOT integrated into the iOS app**. The Xcode project needs manual update to use the new C-based TCTI gadgets.

**Risk Level**: Medium (requires manual Xcode work before iOS testing)

**Mitigation**: Docker tests provide 60% coverage; remaining 40% requires Xcode.
