#ifndef AARCH64_GEN_H
#define AARCH64_GEN_H

/*
 * TCTI Block Generator for aarch64
 *
 * Translates aarch64 instructions into threaded gadget addresses.
 * No runtime code generation - just emits arrays of function pointers.
 */

#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#import <IXLandLinuxRuntime/tcti/frame.h>

// Maximum gadgets per block
#define A64_MAX_GADGETS_PER_BLOCK 512

// Generator error codes
enum a64_gen_error {
    A64_GEN_OK = 0,
    A64_GEN_INVALID_INSN = -1,
    A64_GEN_UNSUPPORTED = -2,
    A64_GEN_TOO_MANY = -3,
    A64_GEN_NO_MEMORY = -4,
};

// Block generation state
typedef struct a64_gen_state {
    // Output buffer
    tcti_gadget_t *gadgets;
    size_t max_gadgets;
    size_t num_gadgets;

    // Current instruction being processed
    uint64_t guest_pc;
    uint32_t raw_insn;
    a64_instr_t decoded;

    // Block metadata
    uint64_t start_pc;
    uint64_t end_pc;  // PC after last instruction
    int is_complete;  // Block has explicit exit

    // Statistics
    int instructions_processed;
} a64_gen_state_t;

// Initialize generator state
int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max);

// Reset generator for new block
void a64_gen_reset(a64_gen_state_t *state, uint64_t pc);

// Generate gadget for single instruction
// Returns: 0 = ok, 1 = block should end, <0 = error
int a64_gen_instruction(a64_gen_state_t *state, uint32_t insn, uint64_t pc);

// Mark block as complete (add exit gadget)
int a64_gen_finalize(a64_gen_state_t *state);

// Complete block generator that processes until branch
// Returns number of instructions processed, or error
// Safety limits: max 50 instructions, max 512 gadget entries per block
int a64_gen_basic_block(a64_gen_state_t *state, struct cpu_state *cpu,
                        struct tlb *tlb, uint64_t *end_pc);

// Internal: Generate for specific instruction categories
int a64_gen_dp_imm(a64_gen_state_t *state, const a64_instr_t *instr);
int a64_gen_dp_reg(a64_gen_state_t *state, const a64_instr_t *instr);
int a64_gen_branch(a64_gen_state_t *state, const a64_instr_t *instr);
int a64_gen_ldst(a64_gen_state_t *state, const a64_instr_t *instr);
int a64_gen_system(a64_gen_state_t *state, const a64_instr_t *instr);

// Helper: Add single gadget to block
static inline int a64_gen_add_gadget(a64_gen_state_t *state, tcti_gadget_t gadget) {
    if (state->num_gadgets >= state->max_gadgets)
        return A64_GEN_TOO_MANY;
    state->gadgets[state->num_gadgets++] = gadget;
    return A64_GEN_OK;
}

// Helper: Get current PC after N gadgets
static inline uint64_t a64_gen_current_pc(const a64_gen_state_t *state) {
    return state->guest_pc;
}

#endif
