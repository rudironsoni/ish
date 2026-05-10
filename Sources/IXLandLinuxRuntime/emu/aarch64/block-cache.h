#ifndef AARCH64_BLOCK_CACHE_H
#define AARCH64_BLOCK_CACHE_H

#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/util/list.h>
#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/util/sync.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declaration for trace sidecar
struct trace_block_sidecar;

// Block cache entry for compiled TCTI blocks
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>

struct a64_block {
    uint64_t start_pc;
    uint64_t end_pc;
    size_t num_gadgets;
    bool explicit_pc_on_exit;

    tcti_gadget_t *gadgets;

    struct list chain;

    /* Executable-code generation at compile time. If executable layout or
       code bytes change after this block was compiled, the block is stale
       and MUST NOT be used. This replaces whole-cache invalidation as the
       primary correctness mechanism. */
    mem_generation_t compile_generation;

    bool is_jetsam;
    struct list jetsam;

    struct trace_block_sidecar *trace_sidecar;
};

// Simple hash table for block lookup
// Using same approach as iSH's asbestos: chained buckets
#define BLOCK_CACHE_HASH_BITS 10
#define BLOCK_CACHE_HASH_SIZE (1 << BLOCK_CACHE_HASH_BITS)

struct a64_block_cache {
    struct list hash[BLOCK_CACHE_HASH_SIZE];
    struct list jetsam;
    size_t num_blocks;
    // lock_t lock;  // TODO: Add locking for thread safety
};

enum a64_cache_lookup_miss_reason {
    A64_CACHE_LOOKUP_HIT = 0,
    A64_CACHE_LOOKUP_MISS_NOT_FOUND,
    A64_CACHE_LOOKUP_MISS_JETSAM,
    A64_CACHE_LOOKUP_MISS_GENERATION,
};

// Initialize block cache
void a64_cache_init(struct a64_block_cache *cache);

// Look up block by PC and current code generation. Returns NULL if not found or stale.
struct a64_block *a64_cache_lookup(struct a64_block_cache *cache, uint64_t pc,
                                   mem_generation_t current_generation);
struct a64_block *a64_cache_lookup_ex(struct a64_block_cache *cache, uint64_t pc,
                                      mem_generation_t current_generation,
                                      enum a64_cache_lookup_miss_reason *miss_reason_out);

// Insert block into cache
void a64_cache_insert(struct a64_block_cache *cache, struct a64_block *block);

// Invalidate all blocks (reserved for exceptional hard invalidation paths)
void a64_cache_invalidate_all(struct a64_block_cache *cache);

// Invalidate blocks overlapping a guest executable address range.
void a64_cache_invalidate_range(struct a64_block_cache *cache, uint64_t start_pc, uint64_t end_pc);

// Free a block
void a64_block_free(struct a64_block *block);

// Compile a basic block starting at PC (defined in cpu.c)
struct a64_block *a64_compile_block(struct cpu_state *cpu, uint64_t pc, struct tlb *tlb);

// Execute a compiled block (defined in cpu.c)
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block);

// Hash function for PC
static inline size_t a64_cache_hash(uint64_t pc)
{
    return ((pc >> 2) & (BLOCK_CACHE_HASH_SIZE - 1));
}

#endif
