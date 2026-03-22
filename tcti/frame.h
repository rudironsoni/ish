#ifndef FIBER_FRAME_H
#define FIBER_FRAME_H

#include <stdatomic.h>
#include "emu/cpu.h"

// keep in sync with asm
#define FIBER_RETURN_CACHE_SIZE 2048  // Reduced from 4096 since we have 2 ways
#define FIBER_RETURN_CACHE_HASH(x) (((x) >> 4) & (FIBER_RETURN_CACHE_SIZE - 1))
#define FIBER_RETURN_CACHE_WAYS 2

// Persistent L0 cache size (must be power of 2)
#define FIBER_EXEC_CTX_CACHE_SIZE 1024
#define FIBER_EXEC_CTX_CACHE_MASK (FIBER_EXEC_CTX_CACHE_SIZE - 1)

// PR 6: Return cache entry for 2-way associative cache
// Simplified: Just store return address and target pointer (16 bytes)
typedef struct {
    uint64_t ret_addr;       // Return address (guest PC)
    void *target_ip_ptr;     // Pointer to call gadget arguments
} ret_cache_entry_t;

// Stats indices for performance tracking
#define STAT_TB_COMPILES         0
#define STAT_TB_L0_HITS          1
#define STAT_TB_L1_HITS          2
#define STAT_TB_CHAIN_PATCH_ATT  3
#define STAT_TB_CHAIN_PATCH_OK   4
#define STAT_RET_CACHE_HITS      5
#define STAT_RET_CACHE_MISSES    6
#define STAT_TLB_READ_HITS       7
#define STAT_TLB_READ_MISSES     8
#define STAT_TLB_WRITE_HITS      9
#define STAT_TLB_WRITE_MISSES    10
#define STAT_INVALIDATED_PAGES   11
#define STAT_RETIRED_BLOCKS      12
#define STAT_HELPER_EXPAND_FLAGS 13
#define STAT_HELPER_COLLAPSE_FLAGS 14
#define STAT_NUM_STATS           16

struct fiber_frame {
    struct cpu_state cpu;
    void *bp;
    addr_t value_addr;
    uint64_t value[2]; // buffer for crosspage crap
    struct fiber_block *last_block;
    // PR 6: 2-way associative return cache (replaces direct-mapped ret_cache)
    ret_cache_entry_t ret_cache[FIBER_RETURN_CACHE_SIZE][FIBER_RETURN_CACHE_WAYS];
    uint8_t ret_cache_lru[FIBER_RETURN_CACHE_SIZE];  // LRU bits for replacement
};

// Persistent execution context - survives across interrupt boundaries
// Attached to CPU/thread state to eliminate per-run allocations
struct fiber_exec_ctx {
    // Persistent frame state (reused across interrupts)
    struct fiber_frame frame;
    
    // Persistent L0 block cache (direct-mapped)
    // Indexed by: (ip ^ (ip >> 12)) % FIBER_EXEC_CTX_CACHE_SIZE
    struct fiber_block *l0_cache[FIBER_EXEC_CTX_CACHE_SIZE];
    
    // Performance counters (incremented during execution)
    uint64_t stats[STAT_NUM_STATS];
    
    // Epoch-based reclamation state (PR 4)
    uint32_t local_epoch;   // Epoch when execution started
    bool active;            // Currently executing (for thread registry)
    
    // Scratch buffer for instrumentation/debugging
    uint64_t last_guest_pc;
    uint64_t last_state_hash;
};

// Get execution context for a CPU (creates if needed)
struct fiber_exec_ctx *fiber_exec_ctx_get(struct cpu_state *cpu);

// Put execution context (mark inactive)
void fiber_exec_ctx_put(struct fiber_exec_ctx *ctx);

// Reset frame for new execution run (clears transient state)
void fiber_exec_ctx_reset(struct fiber_exec_ctx *ctx, struct cpu_state *cpu);

// Increment a stat counter (zero-cost when disabled)
#ifdef ENABLE_PERF_STATS
static inline void fiber_stat_inc(struct fiber_exec_ctx *ctx, int stat) {
    if (ctx && stat >= 0 && stat < STAT_NUM_STATS) {
        ctx->stats[stat]++;
    }
}

static inline uint64_t fiber_stat_get(struct fiber_exec_ctx *ctx, int stat) {
    if (ctx && stat >= 0 && stat < STAT_NUM_STATS) {
        return ctx->stats[stat];
    }
    return 0;
}
#else
#define fiber_stat_inc(ctx, stat) ((void)0)
static inline uint64_t fiber_stat_get(struct fiber_exec_ctx *ctx, int stat) {
    (void)ctx; (void)stat;
    return 0;
}
#endif

// Report TLB statistics for PR 8 analysis
// Call this at interrupt boundaries or on exit to measure TLB performance
static inline void fiber_report_tlb_stats(struct fiber_exec_ctx *ctx) {
    if (!ctx) return;
    
    uint64_t reads = fiber_stat_get(ctx, STAT_TLB_READ_HITS) + fiber_stat_get(ctx, STAT_TLB_READ_MISSES);
    uint64_t writes = fiber_stat_get(ctx, STAT_TLB_WRITE_HITS) + fiber_stat_get(ctx, STAT_TLB_WRITE_MISSES);
    
    if (reads > 0 || writes > 0) {
        // These can be logged or sent to telemetry
        // For now, just placeholders for measurement
        (void)fiber_stat_get(ctx, STAT_TLB_READ_HITS);
        (void)fiber_stat_get(ctx, STAT_TLB_WRITE_HITS);
    }
}

#endif // FIBER_FRAME_H
