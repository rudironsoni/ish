#include "asbestos/aarch64/gadgets.h"
#include "emu/aarch64/cpu.h"
#include <string.h>

// Block cache entry - maps guest addresses to compiled blocks
#define BLOCK_CACHE_BITS 16
#define BLOCK_CACHE_SIZE (1 << BLOCK_CACHE_BITS)
#define BLOCK_CACHE_MASK (BLOCK_CACHE_SIZE - 1)

static a64_fiber_block_t *block_cache[BLOCK_CACHE_SIZE];

// Hash function for block cache
static inline size_t block_hash(uint64_t addr) {
    return (addr >> 2) & BLOCK_CACHE_MASK;
}

// Allocate a new fiber block
a64_fiber_block_t *a64_alloc_block(uint64_t guest_addr, size_t max_gadgets) {
    a64_fiber_block_t *block = malloc(sizeof(a64_fiber_block_t));
    if (!block) return NULL;

    block->code = malloc(sizeof(gadget_fn_t) * (max_gadgets + 1));
    if (!block->code) {
        free(block);
        return NULL;
    }

    block->guest_addr = guest_addr;
    block->num_gadgets = 0;
    block->next = NULL;

    return block;
}

// Free a fiber block
void a64_free_block(a64_fiber_block_t *block) {
    if (block) {
        free(block->code);
        free(block);
    }
}

// Look up a block in the cache
a64_fiber_block_t *a64_lookup_block(uint64_t guest_addr) {
    size_t idx = block_hash(guest_addr);
    a64_fiber_block_t *block = block_cache[idx];

    while (block) {
        if (block->guest_addr == guest_addr)
            return block;
        block = block->next;
    }

    return NULL;
}

// Insert a block into the cache
void a64_insert_block(a64_fiber_block_t *block) {
    size_t idx = block_hash(block->guest_addr);
    block->next = block_cache[idx];
    block_cache[idx] = block;
}

// Invalidate all blocks containing an address range
void a64_invalidate_blocks(uint64_t start, uint64_t end) {
    // Simple implementation: could be optimized
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        a64_fiber_block_t **pp = &block_cache[i];
        while (*pp) {
            a64_fiber_block_t *block = *pp;
            // Check if block overlaps invalidated range
            if (block->guest_addr >= start && block->guest_addr < end) {
                *pp = block->next;
                a64_free_block(block);
            } else {
                pp = &block->next;
            }
        }
    }
}

// Entry point to execute code at a guest address
gadget_fn_t a64_gadget_entry(struct cpu_state *cpu, uint64_t addr) {
    // Look for existing block
    a64_fiber_block_t *block = a64_lookup_block(addr);

    if (!block) {
        // Need to compile a new block
        // This would call into the generator
        // For now, return a special "compile" gadget
        return NULL; // TODO: implement compile path
    }

    // Set PC and return first gadget
    cpu->pc = addr;
    return (gadget_fn_t)block->code[0];
}

// Exit from gadget chain
void a64_gadget_exit(struct cpu_state *cpu, int status) {
    // Save state, handle interrupts, etc.
    // status: 0=normal, 1=interrupt, 2=syscall, 3=page fault

    switch (status) {
        case 0: // Normal exit
            break;
        case 1: // Interrupt
            // Handle pending interrupt
            break;
        case 2: // Syscall
            // SVC was executed, syscall number in x8
            break;
        case 3: // Page fault
            // Handle page fault using fault_addr
            break;
    }
}

// Initialize gadget system
void a64_gadgets_init(void) {
    memset(block_cache, 0, sizeof(block_cache));
}

// Cleanup gadget system
void a64_gadgets_cleanup(void) {
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        a64_fiber_block_t *block = block_cache[i];
        while (block) {
            a64_fiber_block_t *next = block->next;
            a64_free_block(block);
            block = next;
        }
        block_cache[i] = NULL;
    }
}
