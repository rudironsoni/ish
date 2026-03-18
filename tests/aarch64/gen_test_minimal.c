/*
 * Minimal generator implementation for testing without full gadget library
 */

#include <string.h>
#include "asbestos/aarch64/gen.h"

int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max) {
    if (!state || !buffer)
        return A64_GEN_INVALID_INSN;

    memset(state, 0, sizeof(*state));
    state->gadgets = buffer;
    state->max_gadgets = max;
    state->num_gadgets = 0;

    return A64_GEN_OK;
}

void a64_gen_reset(a64_gen_state_t *state, uint64_t pc) {
    if (!state)
        return;

    state->num_gadgets = 0;
    state->start_pc = pc;
    state->guest_pc = pc;
    state->end_pc = pc;
    state->is_complete = 0;
    state->instructions_processed = 0;
}

// a64_gen_add_gadget is defined inline in gen.h

// Minimal finalize that doesn't require actual tcti_exit_block
int a64_gen_finalize(a64_gen_state_t *state) {
    if (!state)
        return A64_GEN_INVALID_INSN;

    // Add a dummy exit gadget (null pointer for testing)
    if (state->num_gadgets >= state->max_gadgets)
        return A64_GEN_TOO_MANY;

    state->gadgets[state->num_gadgets++] = NULL;
    state->is_complete = 1;
    return A64_GEN_OK;
}
