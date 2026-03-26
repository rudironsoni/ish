#ifndef AARCH64_BLOCK_CACHE_H
#define AARCH64_BLOCK_CACHE_H

#include "misc.h"
#include "util/list.h"
#include "util/sync.h"
#include "emu/tlb.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration for trace sidecar
struct trace_block_sidecar;

// Block cache entry for compiled TCTI blocks
#include "gadgets_tcti.h"

struct a64_block {
    uint64_t start_pc;          // Starting guest PC
    uint64_t end_pc;            // Ending PC (one past last instruction)
    size_t num_gadgets;         // Number of gadgets in block
    bool explicit_pc_on_exit;   // Control-flow gadget writes guest PC before exit

    // Gadget chain - array of function pointers
    tcti_gadget_t *gadgets;

    // Hash chain links
    struct list chain;

    // For invalidation tracking
    bool is_jetsam;
    struct list jetsam;
    
    // Debug sidecar for tracing (NULL if tracing not enabled)
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

// Initialize block cache
void a64_cache_init(struct a64_block_cache *cache);

// Look up block by PC. Returns NULL if not found.
struct a64_block *a64_cache_lookup(struct a64_block_cache *cache, uint64_t pc);

// Insert block into cache
void a64_cache_insert(struct a64_block_cache *cache, struct a64_block *block);

// Invalidate all blocks (e.g., after memory write)
void a64_cache_invalidate_all(struct a64_block_cache *cache);

// Free a block
void a64_block_free(struct a64_block *block);

// Compile a basic block starting at PC (defined in cpu.c)
struct a64_block *a64_compile_block(struct cpu_state *cpu, uint64_t pc, struct tlb *tlb);

// Execute a compiled block (defined in cpu.c)
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block);

// Hash function for PC
static inline size_t a64_cache_hash(uint64_t pc) {
    return ((pc >> 2) & (BLOCK_CACHE_HASH_SIZE - 1));
}

#endif
