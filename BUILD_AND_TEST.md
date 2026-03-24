# Build and Test Instructions

## Quick Build Test

Test individual files compile:
```bash
gcc -c -I. -DARCH_AARCH64=1 -DENGINE_TCTI=1 emu/aarch64/cpu.c -o /tmp/cpu.o
gcc -c -I. -DARCH_AARCH64=1 -DENGINE_TCTI=1 emu/aarch64/decode.c -o /tmp/decode.o
gcc -c -I. -DARCH_AARCH64=1 -DENGINE_TCTI=1 tcti/aarch64/gen.c -o /tmp/gen.o
```

## Full Build

This branch is AArch64 guest only using the TCTI execution engine:

```bash
meson setup build
ninja -C build
```

## Performance Testing

### Check Stats After Running
The execution context now tracks these metrics:
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
Run these workloads to verify optimizations:
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

✅ PR 1: Persistent execution context + L0 cache + stats
✅ PR 2: Sticky compiled-page bitmap for invalidation
✅ PR 3: Lockless chain patch fast reject
✅ PR 4: Epoch reclamation (removed jetsam_lock)
✅ PR 5: Decoder hardening + 64-bit atomic counters
✅ PR 6: Return cache 2-way associativity
✅ PR 7: Block allocator with size-class freelists

## Core Files
- `tcti/aarch64/frame.h`
- `tcti/aarch64/gen.c`
- `tcti/aarch64/gen.h`
- `emu/aarch64/cpu.h`
- `emu/aarch64/cpu.c`
- `emu/aarch64/decode.c`
- `emu/tlb.h`
- `kernel/memory.c`
