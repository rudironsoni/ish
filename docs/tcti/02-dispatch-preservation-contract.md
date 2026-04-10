# Dispatch Preservation Contract

## Purpose

This file defines the lawful dispatch chain for TCTI block execution and the preservation guarantees that must hold across gadget dispatch and helper crossings.

## Dispatch Chain

Current repo-local dispatch chain is:
- C code validates block and installs fault containment
- `tcti_entry_block(gadgets, cpu)` establishes carrier state
- first gadget pointer is loaded from `x28`
- each gadget dispatches through epilogue `ldr x27, [x28], #8 ; br x27`
- helper crossings use `_tcti_c_call_prologue` and `_tcti_c_call_epilogue`
- block exits through `tcti_exit_block`

Repo-local proof:
- `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c:1237-1269`
- `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c:1335`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:65-85`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:86-128`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:130-192`

## Bytecode Stream Contract

The gadget stream is an explicit pointer stream in `x28`.

Inline immediates are legal stream elements. The generator may emit a raw `uint64_t` directly into the same stream, to be consumed by the preceding gadget before the next gadget pointer load.

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c:71-88`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:83-85`

## Preservation Requirements

The following must be preserved across helper crossings:
- guest hot carrier registers restored after helper return
- `x28` bytecode pointer restored after helper return
- guest `nzcv` restored after helper return
- helper return must not silently reroute execution to fallback paths

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:91-128`

## Fault Containment Boundary

Host faults during guest execution must be reduced to explicit TCTI fault exits, not process death.

Repo-local proof:
- `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c:1252-1269`
- `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c:1403-1409`

## Illegal Dispatch Patterns

Migration must reject these patterns as invalid:
- hidden runtime dispatcher outside explicit `tcti_entry_block` / epilogue / exit flow
- dynamic case discovery as execution authority
- success-on-fallback behavior after failed gadget dispatch
- broad dispatch claims without proving exact `x28`, `x27`, `nzcv`, and `tcti_exit_reason` boundaries

## Required Proof Surface For Phase 02

Phase `02-dispatch-preservation` must create explicit XCTest-visible proofs for:
- null gadget rejection
- first gadget dispatch
- epilogue next-gadget dispatch
- helper prologue/epilogue preservation of `x28`
- helper prologue/epilogue preservation of `nzcv`
- exit-reason propagation across normal, fault, syscall, signal, complex, and unsupported-sysreg paths where currently defined
