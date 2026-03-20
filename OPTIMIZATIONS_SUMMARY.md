# iSH TCTI Performance Optimizations - Implementation Summary

## Overview
All 7 core performance optimizations have been implemented for the aarch64-only iSH emulator.

## PR 1: Persistent Execution Context
**Files Modified:** `asbestos/frame.h`, `asbestos/asbestos.c`, `emu/aarch64/cpu.h`

**Changes:**
- Added `struct fiber_exec_ctx` with persistent frame, L0 cache, and stats
- Modified `cpu_step_to_interrupt()` to reuse context instead of allocating
- **Result:** Zero allocations in steady-state execution path

## PR 2: Sticky Compiled-Page Bitmap
**Files Modified:** `asbestos/asbestos.h`, `asbestos/asbestos.c`

**Changes:**
- Added `compiled_pages_bitmap` to track which pages have compiled code
- Fast-path check in `asbestos_invalidate_range()` skips non-code pages
- **Result:** Writes to stack/heap/data pages don't trigger expensive invalidation

## PR 3: Lockless Chain Patch Fast Reject
**Files Modified:** `asbestos/asbestos.c`

**Changes:**
- Check if jump slot already patched before taking lock
- Use double-checked locking pattern
- **Result:** Hot edges don't take global lock

## PR 4: Epoch Reclamation
**Files Modified:** `asbestos/asbestos.h`, `asbestos/asbestos.c`

**Changes:**
- 3-bucket epoch system (modulo 3)
- Blocks retired to current epoch, freed after 2 epoch transitions
- Removed `jetsam_lock` from execution path
- **Result:** No stop-the-world reclamation, better concurrency

## PR 5: Decoder Hardening & 64-bit Counters
**Files Modified:** `asbestos/aarch64/gen.c`, `asbestos/aarch64/gen.h`, `emu/tlb.h`, `kernel/memory.c`

**Changes:**
- Block size limits (50 instructions, 256 bytes max)
- Changed `mem_changes` from `unsigned` to `uint64_t`
- Made counter updates atomic
- **Result:** Prevents runaway compilation, thread-safe counters

## PR 6: Return Cache Associativity
**Files Modified:** `asbestos/frame.h`

**Changes:**
- Changed from direct-mapped to 2-way associative
- Added LRU tracking
- **Result:** Reduced aliasing, better hit rates

## PR 7: Block Allocator with Size-Class Freelists
**Files Modified:** `asbestos/asbestos.h`, `asbestos/asbestos.c`, `asbestos/gen.c`

**Changes:**
- 7 size classes: 16, 32, 64, 128, 256, 512, 1024 slots
- Pool capped at 64 blocks per class
- Modified `fiber_block_free()` to use pool
- **Result:** Reduced allocator churn and fragmentation

## Files Changed
- `asbestos/frame.h` - Execution context, return cache
- `asbestos/asbestos.h` - Bitmap, epoch, block pool
- `asbestos/asbestos.c` - All core optimizations
- `asbestos/gen.c` - Block allocation integration
- `asbestos/aarch64/gen.c` - Block size limits
- `asbestos/aarch64/gen.h` - Block size limits
- `emu/aarch64/cpu.h` - Execution context field
- `emu/tlb.h` - 64-bit mem_changes
- `kernel/memory.c` - Atomic counter updates

## Compilation Status
All aarch64 files compile successfully with `gcc -c -I. -DARCH_AARCH64=1`

## Performance Improvements
1. **Zero allocations** in hot execution path
2. **Fast invalidation** for non-code pages
3. **Reduced lock contention** on chain patches
4. **No global lock** during execution
5. **Reduced aliasing** in return cache
6. **Reduced allocator pressure** with block pool

## PR 8: Optional 2-Way TLB (Future Work)
Will implement TLB stats first to measure if needed:
- Read miss rate
- Write miss rate
- Cross-page frequency
- Aliasing rate
