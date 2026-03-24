/*
 * tcti/frame.c - Fiber execution context management
 *
 * Provides per-CPU execution contexts for TCTI with L0 block cache.
 */

#include "tcti/frame.h"
#include "emu/aarch64/cpu.h"
#include <stdlib.h>
#include <string.h>

// Global thread-local storage for execution contexts
// In a multi-threaded system, this would be __thread or pthread_getspecific
static struct fiber_exec_ctx *g_fiber_ctx = NULL;

/*
 * Get execution context for a CPU
 * Creates context on first use, reuses existing context if available
 */
struct fiber_exec_ctx *fiber_exec_ctx_get(struct cpu_state *cpu) {
    // For now, use a single global context
    // In multi-threaded mode, this would be per-thread
    if (g_fiber_ctx == NULL) {
        g_fiber_ctx = calloc(1, sizeof(struct fiber_exec_ctx));
        if (g_fiber_ctx == NULL) {
            return NULL;
        }
        // Initialize defaults - all fields already zeroed by calloc
    }
    
    g_fiber_ctx->active = true;
    // frame.cpu is RESERVED for future fiber work
    // Current execution runs directly on the authoritative cpu_state
    
    return g_fiber_ctx;
}

/*
 * Put execution context (mark inactive)
 * Called when execution is interrupted (syscall, fault, signal)
 */
void fiber_exec_ctx_put(struct fiber_exec_ctx *ctx) {
    if (ctx != NULL) {
        ctx->active = false;
        // Do not free - context is reused across multiple execution runs
    }
}

/*
 * Reset frame for new execution run
 * Clears transient state but preserves accumulated stats
 */
void fiber_exec_ctx_reset(struct fiber_exec_ctx *ctx, struct cpu_state *cpu) {
    if (ctx == NULL) {
        return;
    }
    
    // Clear L0 cache on reset (direct-mapped array of block pointers)
    memset(ctx->l0_cache, 0, sizeof(ctx->l0_cache));
    
    // frame.cpu is RESERVED for future fiber work
    // Current execution runs directly on the authoritative cpu_state
    ctx->active = true;
}
