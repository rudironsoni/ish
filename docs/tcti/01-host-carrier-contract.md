# Host Carrier Contract

## Purpose

This file defines the current repo-local carrier model between guest architectural state and host registers or memory during TCTI execution.

## Active Carrier Sources

Primary repo-local sources:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gadgets_memory.c`

## Authoritative Carrier Map

Current exact entry/exit code proves this hot carrier map:
- guest `x0` -> host `x1`
- guest `x1` -> host `x2`
- guest `x2` -> host `x3`
- guest `x3` -> host `x4`
- guest `x4` -> host `x5`
- guest `x5` -> host `x6`
- guest `x6` -> host `x7`
- guest `x7` -> host `x8`
- guest `x8` -> host `x9`
- guest `x9` -> host `x10`
- guest `x10` -> host `x11`
- guest `x11` -> host `x12`
- guest `x12` -> host `x13`
- guest `x13` -> host `x14`
- guest `x14` -> host `x15`
- guest `x15` -> host `x16`

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:44-61`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:155-171`

## Memory-Backed Guest State

Current code treats these as memory-backed across the TCTI entry/exit boundary:
- guest `x16` through `x30`
- guest `sp`

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c:139-151`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:174-234`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gadgets_memory.c:10-15`

## Memory-Backed Access Helpers

Current helper paths include:
- `_tcti_load_xreg` / `tcti_load_xreg`
- `_tcti_store_xreg` / `tcti_store_xreg`
- `_tcti_load_sp` / `tcti_load_sp`
- `_tcti_store_sp` / `tcti_store_sp`

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:194-234`

## Generator Contract

The generator must emit explicit load/store sequences when source or destination state is memory-backed.

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c:123-151`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c:184-190`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c:221-227`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gen.c:247-253`

## Contradiction Record

There is an important repo-local contradiction to preserve and resolve during migration:
- `tcti_entry.S` exact code loads guest `x15` into host `x16`
- some comments elsewhere describe memory-backed state as `x16-x30` and sometimes discuss load/store helpers starting at `x15`

Migration must trust exact entry/exit code and explicit generator behavior over stale comments.

Relevant files:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:44-61`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:194-218`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/gadgets_memory.c:10-15`

## Reserved Host Roles

Current code uses reserved host roles including:
- `x28` as gadget stream pointer
- `x29` as `cpu_state` pointer
- `x27` as next gadget target in epilogue flow
- `x17`, `x19`, `x24`, `x25`, `x26` in helper/temp roles depending on path

Repo-local proof:
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:4-15`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:37-39`
- `Sources/IXLandLinuxRuntime/tcti/aarch64/tcti_entry.S:83-85`

## First Porting Obligation

Phase `01-gadget-family` must prove carrier correctness explicitly in XCTest-visible code for:
- hot register round-trip
- memory-backed register load/store
- SP load/store
- NZCV preservation across helper call boundaries
