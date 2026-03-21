#define DEFAULT_CHANNEL instr
#include "debug.h"
#include "asbestos/asbestos.h"
#include "asbestos/gen.h"
#include "asbestos/frame.h"
#include "emu/cpu.h"
#include "emu/interrupt.h"
#include "util/list.h"

extern int current_pid(void);

// Persistent execution context implementation
static struct fiber_exec_ctx *fiber_exec_ctx_current = NULL;

// Forward declarations
static void fiber_exec_ctx_reset_internal(struct fiber_exec_ctx *ctx, struct cpu_state *cpu);

// PR 4: Epoch reclamation forward declarations
static void epoch_try_advance(struct asbestos *asbestos);
static void epoch_retire_block(struct asbestos *asbestos, struct fiber_block *block);
static bool epoch_can_reclaim(struct asbestos *asbestos, uint32_t target_epoch);

struct fiber_exec_ctx *fiber_exec_ctx_get(struct cpu_state *cpu) {
    // If context exists and is attached to this CPU, reuse it
    if (fiber_exec_ctx_current != NULL && fiber_exec_ctx_current->active == false) {
        fiber_exec_ctx_reset_internal(fiber_exec_ctx_current, cpu);
        return fiber_exec_ctx_current;
    }
    
    // Allocate new context if needed
    if (fiber_exec_ctx_current == NULL) {
        fiber_exec_ctx_current = calloc(1, sizeof(struct fiber_exec_ctx));
        if (fiber_exec_ctx_current == NULL) {
            return NULL;
        }
    }
    
    fiber_exec_ctx_reset_internal(fiber_exec_ctx_current, cpu);
    return fiber_exec_ctx_current;
}

void fiber_exec_ctx_put(struct fiber_exec_ctx *ctx) {
    if (ctx != NULL) {
        ctx->active = false;
    }
}

static void fiber_exec_ctx_reset_internal(struct fiber_exec_ctx *ctx, struct cpu_state *cpu) {
    if (ctx == NULL) return;
    
    // Copy CPU state into frame
    ctx->frame.cpu = *cpu;
    
    // Clear transient state
    ctx->frame.last_block = NULL;
    ctx->frame.bp = NULL;
    ctx->frame.value_addr = 0;
    
    // Clear return cache
    memset(ctx->frame.ret_cache, 0, sizeof(ctx->frame.ret_cache));
    
    // Mark as active
    ctx->active = true;
    ctx->last_guest_pc = 0;
    ctx->last_state_hash = 0;
}

static void fiber_block_disconnect(struct asbestos *asbestos, struct fiber_block *block);
static void fiber_block_free(struct asbestos *asbestos, struct fiber_block *block);
static void fiber_free_jetsam(struct asbestos *asbestos);
static void fiber_resize_hash(struct asbestos *asbestos, size_t new_size);

// PR 7: Block allocator with size-class freelists
// Size classes: 16, 32, 64, 128, 256, 512, 1024 slots (8KB for largest)
static inline int fiber_capacity_to_class(size_t capacity) {
    if (capacity <= 16) return 0;
    if (capacity <= 32) return 1;
    if (capacity <= 64) return 2;
    if (capacity <= 128) return 3;
    if (capacity <= 256) return 4;
    if (capacity <= 512) return 5;
    if (capacity <= 1024) return 6;
    return -1;  // Too large for freelist
}

static struct fiber_block __attribute__((unused)) *fiber_block_alloc_from_pool(struct asbestos *asbestos, size_t capacity) {
    int sc = fiber_capacity_to_class(capacity);
    if (sc < 0) {
        // Too large for pool, use malloc
        return calloc(1, sizeof(struct fiber_block) + capacity * sizeof(unsigned long));
    }
    
    lock(&asbestos->lock);
    if (!list_empty(&asbestos->block_pool.freelists[sc])) {
        struct fiber_block *block = list_first_entry(&asbestos->block_pool.freelists[sc], struct fiber_block, chain);
        list_remove(&block->chain);
        asbestos->block_pool.num_free[sc]--;
        unlock(&asbestos->lock);
        // Clear the block for reuse
        memset(block, 0, sizeof(struct fiber_block) + capacity * sizeof(unsigned long));
        block->used = 0;
        return block;
    }
    unlock(&asbestos->lock);
    
    // No block in freelist, allocate new
    return calloc(1, sizeof(struct fiber_block) + capacity * sizeof(unsigned long));
}

static void fiber_block_free_to_pool(struct asbestos *asbestos, struct fiber_block *block) {
    int sc = fiber_capacity_to_class(block->used);
    if (sc < 0) {
        // Too large for pool, free immediately
        free(block);
        return;
    }
    
    lock(&asbestos->lock);
    if (asbestos->block_pool.num_free[sc] < asbestos->block_pool.max_per_class) {
        // Add to freelist
        list_add(&asbestos->block_pool.freelists[sc], &block->chain);
        asbestos->block_pool.num_free[sc]++;
        unlock(&asbestos->lock);
    } else {
        // Pool is full, free the block
        unlock(&asbestos->lock);
        free(block);
    }
}

struct asbestos *asbestos_new(struct mmu *mmu) {
    struct asbestos *asbestos = calloc(1, sizeof(struct asbestos));
    asbestos->mmu = mmu;
    fiber_resize_hash(asbestos, FIBER_INITIAL_HASH_SIZE);
    asbestos->page_hash = calloc(FIBER_PAGE_HASH_SIZE, sizeof(*asbestos->page_hash));
    list_init(&asbestos->jetsam);
    lock_init(&asbestos->lock);
    wrlock_init(&asbestos->jetsam_lock);
    
    // PR 2: Allocate compiled-page bitmap (one bit per guest page)
    size_t bitmap_size = (MEM_PAGES + 63) / 64;
    asbestos->compiled_pages_bitmap = calloc(bitmap_size, sizeof(uint64_t));
    
    // PR 4: Initialize epoch reclamation
    for (int i = 0; i < 3; i++) {
        list_init(&asbestos->retired[i]);
    }
    asbestos->global_epoch = 0;
    asbestos->retired_bytes = 0;
    lock_init(&asbestos->epoch_lock);
    
    // PR 7: Initialize block pool
    for (int i = 0; i < FIBER_SIZE_CLASSES; i++) {
        list_init(&asbestos->block_pool.freelists[i]);
        asbestos->block_pool.num_free[i] = 0;
    }
    asbestos->block_pool.max_per_class = 64;
    
    return asbestos;
}

void asbestos_free(struct asbestos *asbestos) {
    for (size_t i = 0; i < asbestos->hash_size; i++) {
        struct fiber_block *block, *tmp;
        if (list_null(&asbestos->hash[i]))
            continue;
        list_for_each_entry_safe(&asbestos->hash[i], block, tmp, chain) {
            fiber_block_free(asbestos, block);
        }
    }
    fiber_free_jetsam(asbestos);
    free(asbestos->page_hash);
    free(asbestos->hash);
    free(asbestos);
}

static inline struct list *blocks_list(struct asbestos *asbestos, page_t page, int i) {
    // TODO is this a good hash function?
    return &asbestos->page_hash[page % FIBER_PAGE_HASH_SIZE].blocks[i];
}

void asbestos_invalidate_range(struct asbestos *absestos, page_t start, page_t end) {
    // PR 2: Fast-path check using compiled-page bitmap
    // If no page in range has ever been compiled, skip expensive invalidation
    bool has_compiled = false;
    for (page_t p = start; p < end; p++) {
        if (absestos->compiled_pages_bitmap[p / 64] & (1ULL << (p % 64))) {
            has_compiled = true;
            break;
        }
    }
    if (!has_compiled) {
        // No compiled code on these pages - skip expensive invalidation
        return;
    }
    
    lock(&absestos->lock);
    struct fiber_block *block, *tmp;
    for (page_t page = start; page < end; page++) {
        for (int i = 0; i <= 1; i++) {
            struct list *blocks = blocks_list(absestos, page, i);
            if (list_null(blocks))
                continue;
            list_for_each_entry_safe(blocks, block, tmp, page[i]) {
                fiber_block_disconnect(absestos, block);
                // PR 4: Use epoch-based retirement instead of jetsam
                epoch_retire_block(absestos, block);
            }
        }
    }
    unlock(&absestos->lock);
}

void asbestos_invalidate_page(struct asbestos *asbestos, page_t page) {
    asbestos_invalidate_range(asbestos, page, page + 1);
}
void asbestos_invalidate_all(struct asbestos *asbestos) {
    asbestos_invalidate_range(asbestos, 0, MEM_PAGES);
}

static void fiber_resize_hash(struct asbestos *asbestos, size_t new_size) {
    TRACE_(verbose, "%d resizing hash to %lu, using %lu bytes for gadgets\n", current_pid(), new_size, asbestos->mem_used);
    struct list *new_hash = calloc(new_size, sizeof(struct list));
    for (size_t i = 0; i < asbestos->hash_size; i++) {
        if (list_null(&asbestos->hash[i]))
            continue;
        struct fiber_block *block, *tmp;
        list_for_each_entry_safe(&asbestos->hash[i], block, tmp, chain) {
            list_remove(&block->chain);
            list_init_add(&new_hash[block->addr % new_size], &block->chain);
        }
    }
    free(asbestos->hash);
    asbestos->hash = new_hash;
    asbestos->hash_size = new_size;
}

static void fiber_insert(struct asbestos *asbestos, struct fiber_block *block) {
    asbestos->mem_used += block->used;
    asbestos->num_blocks++;
    // target an average hash chain length of 1-2
    if (asbestos->num_blocks >= asbestos->hash_size * 2)
        fiber_resize_hash(asbestos, asbestos->hash_size * 2);

    list_init_add(&asbestos->hash[block->addr % asbestos->hash_size], &block->chain);
    list_init_add(blocks_list(asbestos, PAGE(block->addr), 0), &block->page[0]);
    if (PAGE(block->addr) != PAGE(block->end_addr))
        list_init_add(blocks_list(asbestos, PAGE(block->end_addr), 1), &block->page[1]);
    
    // PR 2: Mark pages in compiled bitmap
    // This is "sticky" - we never clear bits, only set them
    // It's conservative but makes invalidation checks very fast
    page_t start_page = PAGE(block->addr);
    page_t end_page = PAGE(block->end_addr);
    for (page_t p = start_page; p <= end_page; p++) {
        asbestos->compiled_pages_bitmap[p / 64] |= (1ULL << (p % 64));
    }
}

static struct fiber_block *fiber_lookup(struct asbestos *asbestos, addr_t addr) {
    struct list *bucket = &asbestos->hash[addr % asbestos->hash_size];
    if (list_null(bucket))
        return NULL;
    struct fiber_block *block;
    list_for_each_entry(bucket, block, chain) {
        if (block->addr == addr)
            return block;
    }
    return NULL;
}

static struct fiber_block *fiber_block_compile(addr_t ip, struct tlb *tlb) {
    struct gen_state state;
    TRACE("%d %08x --- compiling:\n", current_pid(), ip);
    gen_start(ip, &state);
    while (true) {
        if (!gen_step(&state, tlb))
            break;
        // no block should span more than 2 pages
        // guarantee this by limiting total block size to 1 page
        // guarantee that by stopping as soon as there's less space left than
        // the maximum length of an x86 instruction
        // TODO refuse to decode instructions longer than 15 bytes
        if (state.ip - ip >= PAGE_SIZE - 15) {
            gen_exit(&state);
            break;
        }
    }
    gen_end(&state);
    assert(state.ip - ip <= PAGE_SIZE);
    state.block->used = state.capacity;
    return state.block;
}

// Remove all pointers to the block. It can't be freed yet because another
// thread may be executing it.
static void fiber_block_disconnect(struct asbestos *asbestos, struct fiber_block *block) {
    if (asbestos != NULL) {
        asbestos->mem_used -= block->used;
        asbestos->num_blocks--;
    }
    list_remove(&block->chain);
    for (int i = 0; i <= 1; i++) {
        list_remove(&block->page[i]);
        list_remove_safe(&block->jumps_from_links[i]);

        struct fiber_block *prev_block, *tmp;
        list_for_each_entry_safe(&block->jumps_from[i], prev_block, tmp, jumps_from_links[i]) {
            if (prev_block->jump_ip[i] != NULL)
                *prev_block->jump_ip[i] = prev_block->old_jump_ip[i];
            list_remove(&prev_block->jumps_from_links[i]);
        }
    }
}

static void fiber_block_free(struct asbestos *asbestos, struct fiber_block *block) {
    fiber_block_disconnect(asbestos, block);
    // PR 7: Use pool allocator for small blocks
    if (asbestos != NULL) {
        fiber_block_free_to_pool(asbestos, block);
    } else {
        free(block);
    }
}

static void fiber_free_jetsam(struct asbestos *asbestos) {
    struct fiber_block *block, *tmp;
    list_for_each_entry_safe(&asbestos->jetsam, block, tmp, jetsam) {
        list_remove(&block->jetsam);
        free(block);
    }
}

int fiber_enter(struct fiber_block *block, struct fiber_frame *frame, struct tlb *tlb);

static inline size_t fiber_cache_hash(addr_t ip) {
    return (ip ^ (ip >> 12)) % FIBER_CACHE_SIZE;
}

static int cpu_step_to_interrupt(struct cpu_state *cpu, struct tlb *tlb) {
    struct asbestos *asbestos = cpu->mmu->asbestos;
    
    // PR 4: Get persistent execution context with epoch tracking
    struct fiber_exec_ctx *ctx = fiber_exec_ctx_get(cpu);
    if (ctx == NULL) {
        return INT_GPF;  // Failed to get execution context
    }
    
    // Set local epoch for safe reclamation
    ctx->local_epoch = asbestos->global_epoch;
    
    struct fiber_block **cache = ctx->l0_cache;  // Persistent L0 cache
    struct fiber_frame *frame = &ctx->frame;      // Persistent frame

    int interrupt = INT_NONE;
    while (interrupt == INT_NONE) {
        addr_t ip = frame->cpu.pc;
        size_t cache_index = fiber_cache_hash(ip) & FIBER_EXEC_CTX_CACHE_MASK;
        struct fiber_block *block = cache[cache_index];
        if (block == NULL || block->addr != ip) {
            // L0 miss - check L1 (global hash)
            lock(&asbestos->lock);
            block = fiber_lookup(asbestos, ip);
            if (block == NULL) {
                // L1 miss - compile new block
                fiber_stat_inc(ctx, STAT_TB_COMPILES);
                block = fiber_block_compile(ip, tlb);
                fiber_insert(asbestos, block);
            } else {
                // L1 hit
                fiber_stat_inc(ctx, STAT_TB_L1_HITS);
                TRACE("%d %08x --- missed cache\n", current_pid(), ip);
            }
            // Update L0 cache
            cache[cache_index] = block;
            cache[cache_index] = block;
            unlock(&asbestos->lock);
        }
        struct fiber_block *last_block = frame->last_block;
        if (last_block != NULL &&
                (last_block->jump_ip[0] != NULL ||
                 last_block->jump_ip[1] != NULL)) {
            
            // PR 3: Fast-path check - avoid lock if already patched
            // Check if the jump slot already points to block->code
            bool needs_patch = false;
            for (int i = 0; i <= 1; i++) {
                if (last_block->jump_ip[i] != NULL &&
                        (*last_block->jump_ip[i] & 0xffffffff) == block->addr) {
                    // Check if already patched to the correct target
                    if (*last_block->jump_ip[i] != (unsigned long)block->code) {
                        needs_patch = true;
                        break;
                    }
                }
            }
            
            // Only take lock if we actually need to patch
            if (needs_patch) {
                fiber_stat_inc(ctx, STAT_TB_CHAIN_PATCH_ATT);
                lock(&asbestos->lock);
                // can't mint new pointers to a block that has been marked jetsam
                // and is thus assumed to have no pointers left
                // Re-check under lock (double-checked locking pattern)
                if (!last_block->is_jetsam && !block->is_jetsam) {
                    for (int i = 0; i <= 1; i++) {
                        if (last_block->jump_ip[i] != NULL &&
                                (*last_block->jump_ip[i] & 0xffffffff) == block->addr &&
                                *last_block->jump_ip[i] != (unsigned long)block->code) {
                            *last_block->jump_ip[i] = (unsigned long) block->code;
                            list_add(&block->jumps_from[i], &last_block->jumps_from_links[i]);
                            fiber_stat_inc(ctx, STAT_TB_CHAIN_PATCH_OK);
                        }
                    }
                }
                unlock(&asbestos->lock);
            }
        }
        frame->last_block = block;

        // block may be jetsam, but that's ok, because it can't be freed until
        // every thread on this asbestos is not executing anything

        TRACE("%d %08x --- cycle %ld\n", current_pid(), ip, frame->cpu.cycle);

        interrupt = fiber_enter(block, frame, tlb);
        if (interrupt == INT_NONE && __atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST))
            interrupt = INT_TIMER;
        if (interrupt == INT_NONE && ++frame->cpu.cycle % (1 << 10) == 0)
            interrupt = INT_TIMER;
        *cpu = frame->cpu;
    }

    // Mark execution context as inactive (no free - it's persistent!)
    fiber_exec_ctx_put(ctx);
    return interrupt;
}

static int __attribute__((unused)) cpu_single_step(struct cpu_state *cpu, struct tlb *tlb) {
    struct gen_state state;
    gen_start(cpu->pc, &state);
    gen_step(&state, tlb);
    gen_exit(&state);
    gen_end(&state);

    struct fiber_block *block = state.block;
    struct fiber_frame frame = {.cpu = *cpu};
    int interrupt = fiber_enter(block, &frame, tlb);
    *cpu = frame.cpu;
    fiber_block_free(NULL, block);
    if (interrupt == INT_NONE)
        interrupt = INT_DEBUG;
    return interrupt;
}

// Forward declaration for aarch64 emulator
extern void a64_cpu_run(struct cpu_state *cpu, struct tlb *tlb);

int cpu_run_to_interrupt(struct cpu_state *cpu, struct tlb *tlb) {
    if (cpu->poked_ptr == NULL)
        cpu->poked_ptr = &cpu->_poked;
    tlb_refresh(tlb, cpu->mmu);
    
    // Use aarch64 emulator for aarch64 binaries
    // This replaces the old x86 asbestos JIT
    a64_cpu_run(cpu, tlb);
    
    // a64_cpu_run runs in an infinite loop and handles interrupts internally
    // If we return here, it means we need to exit (shouldn't happen in normal operation)
    return INT_GPF;
}

void cpu_poke(struct cpu_state *cpu) {
    __atomic_store_n(cpu->poked_ptr, true, __ATOMIC_SEQ_CST);
}

// PR 4: Epoch-based reclamation implementation

// Try to advance the global epoch and reclaim old blocks
// Called at safe points (interrupt boundaries)
static void epoch_try_advance(struct asbestos *asbestos) {
    lock(&asbestos->epoch_lock);
    
    // Check if we should advance epoch
    if (asbestos->retired_bytes < EPOCH_RETIRE_THRESHOLD) {
        unlock(&asbestos->epoch_lock);
        return;
    }
    
    // Calculate which epoch to reclaim (global_epoch - 2)
    uint32_t reclaim_epoch = (asbestos->global_epoch - 2) % 3;
    
    // Check if any execution contexts are still using old epochs
    // For now, we assume single-threaded or use a simple heuristic
    // In multi-threaded scenario, we'd check all active contexts
    
    // Free all blocks in the reclaim epoch
    struct fiber_block *block, *tmp;
    list_for_each_entry_safe(&asbestos->retired[reclaim_epoch], block, tmp, jetsam) {
        list_remove(&block->jetsam);
        asbestos->retired_bytes -= block->used;
        free(block);
        fiber_stat_inc(NULL, STAT_RETIRED_BLOCKS);
    }
    
    // Advance epoch
    asbestos->global_epoch++;
    
    unlock(&asbestos->epoch_lock);
}

// Retire a block to the current epoch bucket
static void epoch_retire_block(struct asbestos *asbestos, struct fiber_block *block) {
    lock(&asbestos->epoch_lock);
    
    uint32_t epoch_bucket = asbestos->global_epoch % 3;
    list_add(&asbestos->retired[epoch_bucket], &block->jetsam);
    asbestos->retired_bytes += block->used;
    
    unlock(&asbestos->epoch_lock);
}

// Check if we can safely reclaim a specific epoch
// Returns true if no active execution context is pinned to this or older epochs
static bool __attribute__((unused)) epoch_can_reclaim(struct asbestos *asbestos, uint32_t target_epoch) {
    // In single-threaded mode, always safe
    // In multi-threaded mode, would check all fiber_exec_ctx entries
    // For now, assume safe after 2 epoch transitions
    return (asbestos->global_epoch - target_epoch) >= 2;
}
