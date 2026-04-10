# Linux Runtime Contract

## Purpose

This file defines the downstream runtime contract that may only be trusted after phases `00-harness-truth`, `01-gadget-family`, `02-dispatch-preservation`, and `03-state-flush` are explicit `REAL PASS` in XCTest-visible form.

## Runtime Scope

The runtime contract covers repo-local Linux-on-iOS execution surfaces including:
- process entry
- ELF loading
- MMU and ABI boundaries
- syscall delivery
- threads and signals
- distro/runtime compatibility
- PTY and terminal delivery only after lower runtime boundaries are trusted

## Current Repo-Local Runtime Proof Sources

Important implementation files:
- `Sources/IXLandLinuxRuntime/kernel/exec.c`
- `Sources/IXLandLinuxRuntime/kernel/task.c`
- `Sources/IXLandLinuxRuntime/kernel/signal.c`
- `Sources/IXLandLinuxRuntime/kernel/exit.c`
- `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c`
- `Sources/IXLandLinuxRuntime/emu/aarch64/sysreg.c`

Representative legacy contracts that describe old coverage, but are not future authority:
- `Tests/IXLandLinuxRuntimeEnd2EndTests/cases/00-trace-harness/TRACE-001-boundary-smoke/case.yaml`
- `Tests/IXLandLinuxRuntimeEnd2EndTests/cases/02-generator/GEN-001-simple-alu-emission/case.yaml`
- `Tests/IXLandLinuxRuntimeEnd2EndTests/cases/04-mmu-abi/ABI-001-process-entry-stack/case.yaml`
- `Tests/IXLandLinuxRuntimeEnd2EndTests/cases/05-elf-loader/ELF-001-static-hello/case.yaml`
- `Tests/IXLandTerminalEnd2EndTests/cases/10-tooling-stability-ios/TOOL-001-gcc-or-clang-guest-build/case.yaml`
- `Tests/IXLandTerminalEnd2EndTests/cases/10-tooling-stability-ios/STAB-001-shell-loop-stability/case.yaml`

## Runtime Ordering

Migration must treat runtime work in this order:
1. `04-full-runtime`
2. `10-pty-chain`
3. `11-terminal-delivery`

PTY and terminal phases are downstream victims until the lower runtime contract is trusted.

## Required Runtime Truth

Every runtime proof must answer:
- what runtime boundary is enabled
- what lower boundaries are already proven
- what exact failing edge is under test
- what remains disabled
- whether the current failure belongs to architectural contract, carrier mapping, dispatch preservation, state flush, or true downstream runtime behavior

## Forbidden Runtime Claims

The following are invalid:
- claiming terminal failure proves a PTY bug while lower runtime phases are untrusted
- claiming distro or tooling correctness from old YAML case status
- claiming Linux runtime correctness from broad app behavior without exact failing-edge reduction

## Future Rewiring Obligation

When executable proofs move into XCTest and explicit checked-in resources, `project.yml` must stop referencing legacy `Tests/*/cases/**` trees as active resources.
