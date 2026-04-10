# Guest Architectural Contract

## Status

This file is the repo-local semantic contract for the guest architectural state that TCTI execution must preserve.

## Active Authorities

Only these repo-local files define the current contract in this repository scope:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gadgets_memory.c`
- `Sources/IXLandLinuxRuntime/tcti/frame.h`
- `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c`
- `Sources/IXLandLinuxRuntime/emu/aarch64/sysreg.h`
- `Sources/IXLandLinuxRuntime/emu/aarch64/sysreg.c`

Legacy `Tests/*/cases/**` YAML files may describe older expectations, but they are not the future source of authority for migration.

## Authoritative Guest State Owner

The authoritative owner of architected guest CPU state is `struct cpu_state`.

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/frame.h:58-64`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:38`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:138-143`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:155-171`

`fiber_exec_ctx` owns fast-path metadata only. `ctx->frame.cpu` is explicitly reserved and is not the active execution owner.

## Architectural Fields

The guest architectural state that must remain coherent across TCTI block execution includes:
- `x[0]` through `x[30]`
- `sp`
- `pc`
- `pstate` / `nzcv`
- `tcti_exit_reason`
- architected sysreg fields handled by the sysreg layer, including `tpidr_el0`, `tpidrro_el0`, and constants such as `CTR_EL0` / `DCZID_EL0`

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:138-143`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:155-171`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:174-234`
- `Sources/IXLandLinuxRuntime/emu/aarch64/sysreg.c`
- `Sources/IXLandLinuxRuntime/emu/aarch64/sysreg.h`

## Required Entry Conditions

A lawful TCTI block entry requires:
- non-null gadget array pointer
- non-null `cpu_state` pointer
- non-null first gadget when `num_gadgets > 0`
- caller-visible state preserved across host call boundary

Repo-local proof:
- `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c:1237-1250`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:23-39`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:65-76`

## Required Exit Conditions

A lawful TCTI block exit requires:
- `cpu->tcti_exit_reason` written before return to C
- `nzcv` captured into `cpu->pstate` before any flag-clobbering instruction in the exit path
- hot guest registers synchronized back into `cpu_state`
- memory-backed guest registers remain authoritative in memory
- caller callee-saved state restored before returning to C

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:130-192`

## Exact Migration Boundary

The first migration phases must prove, in order:
1. harness truth around explicit `cpu_state` setup and observation
2. carrier mapping truth for hot versus memory-backed guest state
3. dispatch preservation across gadget execution and helper paths
4. state flush truth at `tcti_exit_block`
5. only then wider runtime semantics

## Forbidden Interpretations

The following are invalid for this migration:
- treating YAML case metadata as architectural truth
- treating generated headers as the sole architectural contract
- treating `ctx->frame.cpu` as active execution owner
- claiming architectural correctness from broad runtime behavior without exact boundary proof
