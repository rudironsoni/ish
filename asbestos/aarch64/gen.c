/*
 * TCTI Block Generator for aarch64
 *
 * Translates aarch64 instructions into threaded gadget addresses.
 * No runtime code generation - just emits arrays of function pointers.
 */

#include "asbestos/aarch64/gen.h"
#include <string.h>

// Map guest registers 0-15 to our pre-generated gadget tables
// Registers 16-30 and sp are handled differently (in memory)
#define IS_TCTI_REG(r) ((r) >= 0 && (r) < 16)

// Initialization
int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max) {
    if (!state || !buffer)
        return A64_GEN_INVALID_INSN;

    memset(state, 0, sizeof(*state));
    state->gadgets = buffer;
    state->max_gadgets = max;
    state->num_gadgets = 0;

    return A64_GEN_OK;
}

// Reset for new block
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

// Add a single gadget to the block
static int emit_gadget(a64_gen_state_t *state, tcti_gadget_t gadget) {
    if (state->num_gadgets >= state->max_gadgets)
        return A64_GEN_TOO_MANY;

    state->gadgets[state->num_gadgets++] = gadget;
    return A64_GEN_OK;
}

// Generate gadget for data processing - immediate
int a64_gen_dp_imm(a64_gen_state_t *state, const a64_instr_t *instr) {
    int rd = instr->Rd;
    int rn = instr->Rn;

    // Only handle TCTI-mapped registers (0-15) for now
    if (!IS_TCTI_REG(rd))
        return A64_GEN_UNSUPPORTED;
    if (rn != 31 && !IS_TCTI_REG(rn))
        return A64_GEN_UNSUPPORTED;

    tcti_gadget_t gadget = NULL;

    switch (instr->subtype) {
        case 0: // MOVN/MOVZ/MOVK
            if (instr->imm == 0 && instr->imm_shift == 0) {
                // MOVZ with immediate 0 = clear register
                // Use mov_reg from xzr
                // For now, use the zero-immediate case
                gadget = gadget_mov_imm[rd];
            } else {
                // MOVZ immediate - we need a mov_imm gadget
                gadget = gadget_mov_imm[rd];
            }
            break;

        case 2: // ADD/SUB immediate
            if (IS_TCTI_REG(rn)) {
                // ADD Rd, Rn, #imm
                // For immediate adds, we'd need pre-generated gadgets per immediate
                // For now, fall back to generic handling
                gadget = gadget_add_imm[rd][rn];
            }
            break;

        default:
            return A64_GEN_UNSUPPORTED;
    }

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    return emit_gadget(state, gadget);
}

// Generate gadget for data processing - register
int a64_gen_dp_reg(a64_gen_state_t *state, const a64_instr_t *instr) {
    int rd = instr->Rd;
    int rn = instr->Rn;
    int rm = instr->Rm;

    // Check register ranges
    if (!IS_TCTI_REG(rd) || !IS_TCTI_REG(rn) || !IS_TCTI_REG(rm))
        return A64_GEN_UNSUPPORTED;

    tcti_gadget_t gadget = NULL;

    switch (instr->subtype) {
        case 0: // Logical (AND, BIC, etc.)
            // gadget = gadget_and_reg[rd][rn][rm];
            // Fall through for now
            return A64_GEN_UNSUPPORTED;

        case 1: // ADD register
            gadget = gadget_add_reg[rd][rn][rm];
            break;

        case 3: // SUB register
            gadget = gadget_sub_reg[rd][rn][rm];
            break;

        case 6: // MOV register
            gadget = gadget_mov_reg[rd][rn];
            break;

        default:
            return A64_GEN_UNSUPPORTED;
    }

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    return emit_gadget(state, gadget);
}

// Generate gadget for branch instructions
int a64_gen_branch(a64_gen_state_t *state, const a64_instr_t *instr) {
    // All branches end the current block
    // We emit a special "branch" gadget that handles the transfer

    tcti_gadget_t gadget = NULL;

    switch (instr->subtype) {
        case A64_BRANCH_UNCOND:
            // B (unconditional)
            // Emit branch gadget
            gadget = (tcti_gadget_t)gadget_b;
            break;

        case A64_BRANCH_COND:
            // B.cond
            gadget = (tcti_gadget_t)gadget_bcond;
            break;

        case A64_BRANCH_CMP:
            // CBZ/CBNZ
            gadget = (tcti_gadget_t)(instr->is_64bit ?
                gadget_cbz : gadget_cbnz);
            break;

        case A64_BRANCH_REG:
            // RET/BR/BLR
            gadget = (tcti_gadget_t)gadget_br;
            break;

        default:
            return A64_GEN_UNSUPPORTED;
    }

    if (gadget) {
        emit_gadget(state, gadget);
    }

    // Signal that block should end
    return 1;
}

// Generate gadget for load/store
int a64_gen_ldst(a64_gen_state_t *state, const a64_instr_t *instr) {
    int rt = instr->Rd;  // Target register

    if (!IS_TCTI_REG(rt))
        return A64_GEN_UNSUPPORTED;

    // Load/store operations need TLB translation
    // For now, return unsupported - these need C thunks
    // TODO: Implement load/store thunks
    (void)instr;
    return A64_GEN_UNSUPPORTED;
}

// Generate gadget for system instructions
int a64_gen_system(a64_gen_state_t *state, const a64_instr_t *instr) {
    tcti_gadget_t gadget = NULL;

    switch (instr->subtype) {
        case 0: // SVC
            // Syscall ends the block
            gadget = (tcti_gadget_t)gadget_svc;
            if (gadget)
                emit_gadget(state, gadget);
            return 1;  // End block

        case 2: // MRS
            // System register read
            // TODO: Handle different system registers
            gadget = (tcti_gadget_t)gadget_mrs;
            break;

        case 3: // MSR (immediate)
        case 4: // MSR (register)
            gadget = (tcti_gadget_t)gadget_msr;
            break;

        case 5: // HINT, barriers
            gadget = (tcti_gadget_t)gadget_nop;
            break;

        default:
            return A64_GEN_UNSUPPORTED;
    }

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    return emit_gadget(state, gadget);
}

// Generate gadget for single instruction
// Returns: 0 = ok, 1 = block should end, <0 = error
int a64_gen_instruction(a64_gen_state_t *state, uint32_t insn, uint64_t pc) {
    if (!state)
        return A64_GEN_INVALID_INSN;

    a64_instr_t decoded;
    int ret = a64_decode(insn, &decoded);
    if (ret != 0)
        return A64_GEN_INVALID_INSN;

    // Update state
    state->raw_insn = insn;
    state->decoded = decoded;
    state->guest_pc = pc;

    // Process by category
    switch (decoded.cat) {
        case A64_DP_IMM:
            ret = a64_gen_dp_imm(state, &decoded);
            break;

        case A64_DP_REG:
        case A64_DP_REG2:
            ret = a64_gen_dp_reg(state, &decoded);
            break;

        case A64_BRANCH:
        case A64_BRANCH2:
            ret = a64_gen_branch(state, &decoded);
            break;

        case A64_LD_ST:
            ret = a64_gen_ldst(state, &decoded);
            break;

        // System instructions are handled in A64_BRANCH category
        // The decode function categorizes them appropriately

        case A64_RESERVED:
        case A64_RESERVED2:
            // Treat as NOP for now
            ret = emit_gadget(state, (tcti_gadget_t)gadget_nop);
            break;

        default:
            ret = A64_GEN_UNSUPPORTED;
    }

    if (ret >= 0) {
        state->instructions_processed++;
        state->end_pc = pc + 4;
    }

    return ret;
}

// Mark block as complete
int a64_gen_finalize(a64_gen_state_t *state) {
    if (!state)
        return A64_GEN_INVALID_INSN;

    // Add exit gadget
    tcti_gadget_t exit_gadget = (tcti_gadget_t)tcti_exit_block;
    int ret = emit_gadget(state, exit_gadget);
    if (ret != A64_GEN_OK)
        return ret;

    state->is_complete = 1;
    return A64_GEN_OK;
}

// Generate a complete basic block
int a64_gen_basic_block(a64_gen_state_t *state, struct cpu_state *cpu,
                        uint64_t start_pc, uint64_t *end_pc) {
    if (!state || !cpu)
        return A64_GEN_INVALID_INSN;

    a64_gen_reset(state, start_pc);

    // Read and translate instructions until block end
    // For now, process a fixed number or until error
    int max_insns = 50;  // Reasonable limit

    for (int i = 0; i < max_insns; i++) {
        // Read instruction from guest memory
        uint32_t insn = 0;
        // TODO: Use proper guest memory read
        // For now, this would need integration with the MMU

        int ret = a64_gen_instruction(state, insn, state->guest_pc);

        if (ret < 0) {
            // Error - block incomplete
            return ret;
        }

        if (ret == 1) {
            // Block should end
            break;
        }
    }

    // Finalize the block
    int ret = a64_gen_finalize(state);
    if (ret != A64_GEN_OK)
        return ret;

    if (end_pc)
        *end_pc = state->end_pc;

    return state->instructions_processed;
}
