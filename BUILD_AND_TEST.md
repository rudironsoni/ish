# Build and Test Instructions

**iOS-Only Test Policy**: All tests MUST execute against iOS Simulator or iOS device only.

## Build

This branch is AArch64 guest only using the TCTI execution engine.

### Core Library (meson/ninja)

```bash
meson setup build
ninja -C build
```

### iOS App (Xcode)

```bash
xcodebuild build -project iSH.xcodeproj -scheme iSH -destination 'platform=iOS Simulator,name=iPhone 17 Pro'
```

## Test

### iOS Unit Tests (XCTest)

Run decoder and gadget tests:

```bash
xcodebuild test -project iSH.xcodeproj -scheme UnitTests -destination 'platform=iOS Simulator,name=iPhone 17 Pro'
```

### iOS UI Tests (XCTest)

Run emulator smoke tests:

```bash
xcodebuild test -project iSH.xcodeproj -scheme iSHUITests -destination 'platform=iOS Simulator,name=iPhone 17 Pro'
```

## Performance Testing

### Check Stats After Running
The execution context tracks these metrics:
- `STAT_TB_COMPILES` - Translation block compilations
- `STAT_TB_L0_HITS` - L0 cache hits
- `STAT_TB_L1_HITS` - L1 (global hash) hits
- `STAT_TB_CHAIN_PATCH_ATT` - Chain patch attempts
- `STAT_TB_CHAIN_PATCH_OK` - Successful chain patches
- `STAT_TLB_READ_HITS/MISSES` - TLB read performance
- `STAT_TLB_WRITE_HITS/MISSES` - TLB write performance
- `STAT_INVALIDATED_PAGES` - Page invalidations
- `STAT_RETIRED_BLOCKS` - Blocks retired via epoch

### Acceptance Tests
Run these workloads in the iOS emulator to verify optimizations:
1. Self-modifying code on single page
2. Self-modifying code across page boundary
3. Deep call/ret recursion (>1000 levels)
4. Signal/timer interrupt in hot loop
5. Fork + COW write into executed page
6. Cross-page push/pop/call/ret
7. Tight shell loop: `while :; do :; done`
8. Large file copy: `cp 1GB.file /tmp/`
9. Package install: `apk add` or equivalent

### Benchmark Rule
Do not consider optimizations successful unless they show:
- Lower heap allocations (should be 0 in steady-state)
- Improved L0 cache hit rate (target >90%)
- Reduced invalidation overhead

## PR Implementation Status

- PR 1: Persistent execution context + L0 cache + stats
- PR 2: Sticky compiled-page bitmap for invalidation
- PR 3: Lockless chain patch fast reject
- PR 4: Epoch reclamation (removed jetsam_lock)
- PR 5: Decoder hardening + 64-bit atomic counters
- PR 6: Return cache 2-way associativity
- PR 7: Block allocator with size-class freelists

## Core Files

- `tcti/aarch64/frame.h`
- `tcti/aarch64/gen.c`
- `tcti/aarch64/gen.h`
- `emu/aarch64/cpu.h`
- `emu/aarch64/cpu.c`
- `emu/aarch64/decode.c`
- `emu/tlb.h`
- `kernel/memory.c`

## Test Structure

### Deterministic Core Tests

Location: `app/Tests/`

- **Aarch64DecoderTests.m** - Instruction decoder validation
- **Aarch64GadgetTests.m** - Gadget semantics (NZCV, branches, load/store)

### Runtime Proof Tests

Location: `app/Tests/`

- **Aarch64RuntimeProofTests.m** - APPSIM scenario assertions

### UI Smoke Tests

Location: `app/UITests/`

- **Aarch64EmulatorTests.m** - App launch, terminal surface validation

## Machine-Readable Results

Test runs emit `artifacts/test-summary.json` with:

```json
{
  "active_case": "",
  "source_tree_commit_sha": "",
  "working_tree_state": "",
  "deterministic_core_status": "",
  "ios_runtime_status": "",
  "ios_ui_smoke_status": "",
  "simulator_identity": "",
  "runtime_mode": "",
  "first_failing_layer": "",
  "failing_test_or_checkpoint": "",
  "next_narrow_step": ""
}
```
