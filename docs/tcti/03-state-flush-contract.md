# State Flush Contract

## Purpose

This file defines the exact boundary where transient host carrier state becomes authoritative guest architectural state again.

## Canonical Flush Site

The canonical flush site is `tcti_exit_block`.

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:130-192`

## Required Flush Order

Current code requires this order:
1. write `cpu->tcti_exit_reason`
2. capture live `nzcv` into `cpu->pstate` before any flag-clobbering instruction in the exit path
3. store hot carrier registers back into guest `x0-x15`
4. leave memory-backed guest state authoritative in memory
5. restore caller-preserved host state and return

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:136-192`

## Flush Surface

State flush must cover at least:
- `tcti_exit_reason`
- `pstate` / `nzcv`
- guest `x0-x15` from host carriers
- guest `x16-x30` and `sp` through their memory-backed ownership model
- `pc` progression as observed by post-exit C-side execution

## Why This Is A Distinct Phase

Generator success is not enough.
Dispatch preservation is not enough.
A gadget stream may execute and still lose guest truth if the flush boundary is wrong.

That is why `03-state-flush` is its own migration gate.

## Current Risk Sites

Current repo-local risk sites that later cleanup will need to shrink or replace with narrower proof include:
- hardcoded PC-specific tracing in `Sources/IXLandLinuxRuntime/emu/aarch64/cpu.c:1271-1402`
- diagnostic-heavy helper tracing in `Sources/IXLandLinuxRuntime/tcti/aarch64/gadgets_memory.c`
- leftover probe globals in `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:239-268`

These observations do not change the canonical flush site.
They identify areas where proof noise currently obscures the boundary.

## Required Proof Surface For Phase 03

Phase `03-state-flush` must explicitly prove:
- `TCTI_EXIT_NORMAL` flushes hot guest registers correctly
- `TCTI_EXIT_FAULT` flushes exact failure state without dropping prior guest truth
- `nzcv` survives normal and helper-assisted exit paths
- memory-backed register modifications are visible after exit
- caller-preserved host state is restored to C
