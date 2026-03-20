# TLB Instrumentation for PR 8 Analysis

## Overview
Added TLB statistics tracking to measure if 2-way associative TLB (PR 8) is needed.

## Changes Made

### 1. TLB Structure (`emu/tlb.h`)
Added `stats_ctx` field to track statistics:
```c
struct tlb {
    // ... existing fields ...
    struct fiber_exec_ctx *stats_ctx;  // NULL if stats not being tracked
};
```

### 2. TLB Miss Tracking (`emu/tlb.c`)
Modified `tlb_handle_miss()` to count misses:
```c
if (tlb->stats_ctx) {
    if (type == MEM_READ) {
        fiber_stat_inc(tlb->stats_ctx, STAT_TLB_READ_MISSES);
    } else {
        fiber_stat_inc(tlb->stats_ctx, STAT_TLB_WRITE_MISSES);
    }
}
```

### 3. Statistics Reporting (`asbestos/frame.h`)
Added `fiber_report_tlb_stats()` function to analyze TLB performance.

## Metrics Available

The following counters are now tracked:

- `STAT_TLB_READ_HITS` - Read operations that hit in TLB
- `STAT_TLB_READ_MISSES` - Read operations that missed
- `STAT_TLB_WRITE_HITS` - Write operations that hit in TLB  
- `STAT_TLB_WRITE_MISSES` - Write operations that missed

## Usage

To enable TLB statistics tracking:
```c
tlb->stats_ctx = fiber_exec_ctx_get(cpu);
```

To calculate hit rates:
```c
uint64_t reads = STAT_TLB_READ_HITS + STAT_TLB_READ_MISSES;
uint64_t read_hit_rate = (reads > 0) ? (100 * STAT_TLB_READ_HITS / reads) : 0;
```

## PR 8 Decision Criteria

Implement 2-way TLB if:
- Read miss rate > 5%
- Write miss rate > 5%
- High aliasing detected (frequent collisions on different pages)

## Current Status

✅ TLB instrumentation added and compiling
✅ Ready for runtime measurement
⏳ PR 8 (2-way TLB) pending measurement results
