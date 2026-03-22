/*
 * Minimal generator implementation for testing without full gadget library
 */

#include <string.h>
#include "tcti/aarch64/gen.h"

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

int a64_gen_instruction(a64_gen_state_t *state, uint32_t insn, uint64_t pc) {
    if (!state)
        return A64_GEN_INVALID_INSN;

    state->raw_insn = insn;
    state->guest_pc = pc;

    a64_instr_t decoded;
    int ret = a64_decode(insn, &decoded);
    if (ret != 0)
        return ret;

    state->decoded = decoded;
    state->instructions_processed++;

    if (decoded.cat == A64_BRANCH2) {
        return 1;
    }

    a64_gen_add_gadget(state, NULL);
    return 0;
}

// Minimal finalize that doesn't require actual tcti_exit_block
int a64_gen_finalize(a64_gen_state_t *state) {
    if (!state)
        return A64_GEN_INVALID_INSN;

    if (state->num_gadgets >= state->max_gadgets)
        return A64_GEN_TOO_MANY;

    state->gadgets[state->num_gadgets++] = NULL;
    state->is_complete = 1;
    return A64_GEN_OK;
}
