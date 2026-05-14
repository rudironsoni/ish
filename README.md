# iSH / IXLand AArch64 Runtime

This repository runs a Linux userspace shell experience on iOS by executing guest AArch64 code through a TCTI engine and translating Linux syscalls into host-backed runtime behavior.

## Act 1 — Product

### What This Product Is

This product is a Linux shell experience on iPhone and iPad. Users open the app, type commands, and expect a real Linux-style terminal workflow: packages, scripts, shell tools, and interactive sessions that behave consistently.

The app surface is `IXLandTerminal`. The user expectation is simple:

- Commands run when typed.
- Output appears correctly.
- Prompt returns reliably.
- Sessions stay alive unless the user exits.

### Why `OrlixKernel` Exists

`OrlixKernel` is the long-term direction for giving the product a stronger Linux-owned core instead of relying on app-layer or host-layer shortcuts for semantics.  
From a user perspective, this matters because Linux behavior quality depends on kernel-owned contracts:

- Process lifecycle and wait/exit behavior
- PTY, job-control, and signal semantics
- Filesystem and fd semantics
- Syscall behavior matching Linux expectations

Without that kernel-shaped ownership, users see instability as broken tools, hanging prompts, incorrect statuses, or crashes.

### User-Facing Goals

The product goals are user-visible, not just internal:

- High command compatibility for daily shell usage
- Stable interactive sessions across long runs
- Predictable behavior for shell pipelines and scripting
- Faster startup and command turnaround
- Fewer terminal/session crash exits

### User-Facing Features (Current Direction)

- Interactive Linux shell on iOS terminal UI
- AArch64 guest command execution through TCTI
- Rootfs-based Linux userspace workflows
- App-level terminal session lifecycle and restart handling
- Runtime/system/contract test layers focused on real regressions

### North Star

Linux on iOS should feel dependable and fast enough for real work, not just demos.

## Act 2 — Technical

## Current AArch64 / TCTI Direction

This branch is AArch64 guest-only and uses TCTI as the execution engine for guest instructions.

TCTI model summary:

- Decode guest AArch64 instructions.
- Lower instructions into gadget sequences.
- Execute gadgets in threaded style (tailcall-driven flow).
- Exit to runtime for syscalls, faults, and signal boundaries.

Key migration constraints in this repo:

- No separate interpreter fallback for guest AArch64 execution.
- Runtime and contract work should advance TCTI path correctness directly.
- Linux-facing semantics remain kernel-owned in runtime code; host mechanics stay in host-adapter seams.

## Project Layout

Top-level runtime and app ownership:

- `Sources/IXLandLinuxRuntime/`
  Linux-facing runtime core: kernel/syscall, fs, task/signal/process, memory, emulation bridge.
- `Sources/IXLandLinuxRuntime/emu/aarch64/`
  AArch64 CPU loop, fetch/decode integration, block cache, exit/fault/syscall boundaries.
- `Sources/IXLandLinuxRuntime/tcti/`
  TCTI lowering and gadget surfaces (including generated gadget headers during build).
- `internal/ios/`
  Host-side iOS/Darwin bridge mechanics (filesystem and platform seams).
- `Sources/IXLandTerminal/`
  iOS app shell, lifecycle, terminal UI wiring, session orchestration.

Test ownership:

- `Tests/IXLandLinuxRuntimeContractTests/`
  TCTI contract tests (fetch/decode/lowering/gadgets/semantic boundaries).
- `Tests/IXLandLinuxRuntimeSystemTests/`
  Real guest-runtime behavior tests (pty, shell behavior, command execution, ABI/system flows).
- `Tests/IXLandLinuxRuntimePerfTests/`
  Runtime and hot-path performance coverage.
- `Tests/IXLandTerminalEnd2EndTests/`
  App-level E2E user-path verification on simulator.

Planning/docs:

- `docs/plans/a64-tcti-proof-program.md`
  Canonical local proof/migration plan for AArch64 TCTI coverage.

## Key Decisions

1. AArch64 guest execution is TCTI-first.
   Runtime correctness issues should be proven and fixed on the real path.

2. Product-visible runtime behavior is the completion bar.
   Internal contract suites are required, but not sufficient by themselves.

3. Separation of concerns:
   - Linux semantics in runtime/kernel paths.
   - Host bridge mechanics in `internal/ios`.
   - UI/session orchestration in `IXLandTerminal`.

4. Codebase truth lives in project/build/runtime sources.
   In practice:
   - `project.yml` is the source of truth for Xcode project generation and schemes.
   - Tests are expected to run on simulator with the runtime and app schemes.

## Goals

Near-term goals:

- Remove remaining guest crash/hang boundaries in common command flows.
- Expand owner-surface test coverage across decode/lowering/gadget/register/semantic layers.
- Keep regressions pinned to narrow runtime contracts before implementation changes.

Medium-term goals:

- Improve command/tool compatibility breadth for typical Linux shell usage.
- Increase stability under interactive PTY workloads.
- Reduce hot-path overhead in decode/lowering/block execution.

North-star goals:

- Linux guest sessions that feel dependable for daily terminal usage on iOS.
- Strong runtime correctness under App Store constraints (JIT-less execution path).
- Sustained performance improvements without compromising Linux-shaped behavior.

## Build and Test

### Prerequisites

- Xcode toolchain
- Python 3
- C/ObjC toolchain dependencies used by the repo

### Build (Xcode)

- `project.yml` defines targets and schemes.
- Canonical schemes in this branch:
  - `IXLandApp-6.12-arm64`
  - `IXLandRuntime-6.12-arm64`

### Simulator Defaults Used by This Repo

- Simulator: `iPhone 17`
- DerivedData: `/Volumes/1TB/Xcode/DerivedData`
- Xcode caches: `/Volumes/1TB/Xcode/Caches`

### Test Layers

Run by intent:

- Runtime contract/internals: `IXLandRuntime-6.12-arm64`
- App E2E paths: `IXLandApp-6.12-arm64`

Use focused tests for defect isolation, then rerun broader suite slices to confirm no regressions.

## Notes

- This README documents the current local architecture and migration direction for this branch.
- For deep migration proof structure and family ownership, use:
  `docs/plans/a64-tcti-proof-program.md`.
