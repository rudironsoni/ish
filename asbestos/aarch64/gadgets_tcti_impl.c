/*
 * TCTI Gadget Implementations
 *
 * Pre-compiled gadgets for aarch64 threaded interpreter.
 * Each gadget performs an operation and chains to the next gadget.
 */

#include "asbestos/aarch64/gadgets_tcti.h"

/*
 * Gadget epilogue - every gadget ends with this.
 * Loads next gadget address from bytecode stream and branches to it.
 */
#define GADGET_EPILOGUE \
    "ldr x27, [x28], #8\n\t" \
    "br x27\n\t"

/*
 * Register assignments:
 * x1-x16  = guest registers x0-x15
 * x27     = temporary/scratch
 * x28     = bytecode stream pointer
 * x29/x30 = preserved
 *
 * Note: Guest x16-x30 and sp are stored in memory (struct cpu_state)
 * and loaded/stored when needed.
 */

/* Helper to get register name */
#define XREG(n) x##n
#define WREG(n) w##n

/*
 * MOVE Register Gadgets
 * mov Rd, Rn
 */

#define EMIT_MOV_REG(rd, rn) \
    __attribute__((naked)) void gadget_mov_reg_##rd##_##rn(void) \
    { \
        asm volatile( \
            "mov %0, %1\n\t" \
            GADGET_EPILOGUE \
            : : "r"(XREG(rd)), "r"(XREG(rn)) : "x27", "x28" \
        ); \
    }

// Generate mov_reg gadgets: 16 * 16 = 256 gadgets
#define EMIT_MOV_REG_ROW(rd) \
    EMIT_MOV_REG(rd, 1) EMIT_MOV_REG(rd, 2) EMIT_MOV_REG(rd, 3) EMIT_MOV_REG(rd, 4) \
    EMIT_MOV_REG(rd, 5) EMIT_MOV_REG(rd, 6) EMIT_MOV_REG(rd, 7) EMIT_MOV_REG(rd, 8) \
    EMIT_MOV_REG(rd, 9) EMIT_MOV_REG(rd, 10) EMIT_MOV_REG(rd, 11) EMIT_MOV_REG(rd, 12) \
    EMIT_MOV_REG(rd, 13) EMIT_MOV_REG(rd, 14) EMIT_MOV_REG(rd, 15) EMIT_MOV_REG(rd, 16)

EMIT_MOV_REG_ROW(1) EMIT_MOV_REG_ROW(2) EMIT_MOV_REG_ROW(3) EMIT_MOV_REG_ROW(4)
EMIT_MOV_REG_ROW(5) EMIT_MOV_REG_ROW(6) EMIT_MOV_REG_ROW(7) EMIT_MOV_REG_ROW(8)
EMIT_MOV_REG_ROW(9) EMIT_MOV_REG_ROW(10) EMIT_MOV_REG_ROW(11) EMIT_MOV_REG_ROW(12)
EMIT_MOV_REG_ROW(13) EMIT_MOV_REG_ROW(14) EMIT_MOV_REG_ROW(15) EMIT_MOV_REG_ROW(16)

// Gadget lookup table for mov_reg
const tcti_gadget_t gadget_mov_reg[16][16] = {
#define MOV_REG_ROW(rd) { \
    gadget_mov_reg_##rd##_1, gadget_mov_reg_##rd##_2, gadget_mov_reg_##rd##_3, gadget_mov_reg_##rd##_4, \
    gadget_mov_reg_##rd##_5, gadget_mov_reg_##rd##_6, gadget_mov_reg_##rd##_7, gadget_mov_reg_##rd##_8, \
    gadget_mov_reg_##rd##_9, gadget_mov_reg_##rd##_10, gadget_mov_reg_##rd##_11, gadget_mov_reg_##rd##_12, \
    gadget_mov_reg_##rd##_13, gadget_mov_reg_##rd##_14, gadget_mov_reg_##rd##_15, gadget_mov_reg_##rd##_16 \
}
    MOV_REG_ROW(1), MOV_REG_ROW(2), MOV_REG_ROW(3), MOV_REG_ROW(4),
    MOV_REG_ROW(5), MOV_REG_ROW(6), MOV_REG_ROW(7), MOV_REG_ROW(8),
    MOV_REG_ROW(9), MOV_REG_ROW(10), MOV_REG_ROW(11), MOV_REG_ROW(12),
    MOV_REG_ROW(13), MOV_REG_ROW(14), MOV_REG_ROW(15), MOV_REG_ROW(16)
};

/*
 * ADD Register Gadgets
 * add Rd, Rn, Rm
 */

#define EMIT_ADD_REG(rd, rn, rm) \
    __attribute__((naked)) void gadget_add_reg_##rd##_##rn##_##rm(void) \
    { \
        asm volatile( \
            "add %0, %1, %2\n\t" \
            GADGET_EPILOGUE \
            : : "r"(XREG(rd)), "r"(XREG(rn)), "r"(XREG(rm)) : "x27", "x28" \
        ); \
    }

// Generate add_reg gadgets: 16 * 16 * 16 = 4096 gadgets
// Using nested macros
#define EMIT_ADD_REG_RN_RM(rd, rn) \
    EMIT_ADD_REG(rd, rn, 1) EMIT_ADD_REG(rd, rn, 2) EMIT_ADD_REG(rd, rn, 3) EMIT_ADD_REG(rd, rn, 4) \
    EMIT_ADD_REG(rd, rn, 5) EMIT_ADD_REG(rd, rn, 6) EMIT_ADD_REG(rd, rn, 7) EMIT_ADD_REG(rd, rn, 8) \
    EMIT_ADD_REG(rd, rn, 9) EMIT_ADD_REG(rd, rn, 10) EMIT_ADD_REG(rd, rn, 11) EMIT_ADD_REG(rd, rn, 12) \
    EMIT_ADD_REG(rd, rn, 13) EMIT_ADD_REG(rd, rn, 14) EMIT_ADD_REG(rd, rn, 15) EMIT_ADD_REG(rd, rn, 16)

#define EMIT_ADD_REG_ROW(rd) \
    EMIT_ADD_REG_RN_RM(rd, 1) EMIT_ADD_REG_RN_RM(rd, 2) EMIT_ADD_REG_RN_RM(rd, 3) EMIT_ADD_REG_RN_RM(rd, 4) \
    EMIT_ADD_REG_RN_RM(rd, 5) EMIT_ADD_REG_RN_RM(rd, 6) EMIT_ADD_REG_RN_RM(rd, 7) EMIT_ADD_REG_RN_RM(rd, 8) \
    EMIT_ADD_REG_RN_RM(rd, 9) EMIT_ADD_REG_RN_RM(rd, 10) EMIT_ADD_REG_RN_RM(rd, 11) EMIT_ADD_REG_RN_RM(rd, 12) \
    EMIT_ADD_REG_RN_RM(rd, 13) EMIT_ADD_REG_RN_RM(rd, 14) EMIT_ADD_REG_RN_RM(rd, 15) EMIT_ADD_REG_RN_RM(rd, 16)

EMIT_ADD_REG_ROW(1) EMIT_ADD_REG_ROW(2) EMIT_ADD_REG_ROW(3) EMIT_ADD_REG_ROW(4)
EMIT_ADD_REG_ROW(5) EMIT_ADD_REG_ROW(6) EMIT_ADD_REG_ROW(7) EMIT_ADD_REG_ROW(8)
EMIT_ADD_REG_ROW(9) EMIT_ADD_REG_ROW(10) EMIT_ADD_REG_ROW(11) EMIT_ADD_REG_ROW(12)
EMIT_ADD_REG_ROW(13) EMIT_ADD_REG_ROW(14) EMIT_ADD_REG_ROW(15) EMIT_ADD_REG_ROW(16)

// Lookup table for add_reg (3D array)
#define ADD_REG_RN(rd, rn) { \
    gadget_add_reg_##rd##_##rn##_1, gadget_add_reg_##rd##_##rn##_2, \
    gadget_add_reg_##rd##_##rn##_3, gadget_add_reg_##rd##_##rn##_4, \
    gadget_add_reg_##rd##_##rn##_5, gadget_add_reg_##rd##_##rn##_6, \
    gadget_add_reg_##rd##_##rn##_7, gadget_add_reg_##rd##_##rn##_8, \
    gadget_add_reg_##rd##_##rn##_9, gadget_add_reg_##rd##_##rn##_10, \
    gadget_add_reg_##rd##_##rn##_11, gadget_add_reg_##rd##_##rn##_12, \
    gadget_add_reg_##rd##_##rn##_13, gadget_add_reg_##rd##_##rn##_14, \
    gadget_add_reg_##rd##_##rn##_15, gadget_add_reg_##rd##_##rn##_16 \
}

#define ADD_REG_ROW(rd) { ADD_REG_RN(rd, 1), ADD_REG_RN(rd, 2), ADD_REG_RN(rd, 3), ADD_REG_RN(rd, 4), \
    ADD_REG_RN(rd, 5), ADD_REG_RN(rd, 6), ADD_REG_RN(rd, 7), ADD_REG_RN(rd, 8), \
    ADD_REG_RN(rd, 9), ADD_REG_RN(rd, 10), ADD_REG_RN(rd, 11), ADD_REG_RN(rd, 12), \
    ADD_REG_RN(rd, 13), ADD_REG_RN(rd, 14), ADD_REG_RN(rd, 15), ADD_REG_RN(rd, 16) }

const tcti_gadget_t gadget_add_reg[16][16][16] = {
    ADD_REG_ROW(1), ADD_REG_ROW(2), ADD_REG_ROW(3), ADD_REG_ROW(4),
    ADD_REG_ROW(5), ADD_REG_ROW(6), ADD_REG_ROW(7), ADD_REG_ROW(8),
    ADD_REG_ROW(9), ADD_REG_ROW(10), ADD_REG_ROW(11), ADD_REG_ROW(12),
    ADD_REG_ROW(13), ADD_REG_ROW(14), ADD_REG_ROW(15), ADD_REG_ROW(16)
};

/*
 * SUB Register Gadgets (similar to ADD)
 */

#define EMIT_SUB_REG(rd, rn, rm) \
    __attribute__((naked)) void gadget_sub_reg_##rd##_##rn##_##rm(void) \
    { \
        asm volatile( \
            "sub %0, %1, %2\n\t" \
            GADGET_EPILOGUE \
            : : "r"(XREG(rd)), "r"(XREG(rn)), "r"(XREG(rm)) : "x27", "x28" \
        ); \
    }

#define EMIT_SUB_REG_RN_RM(rd, rn) \
    EMIT_SUB_REG(rd, rn, 1) EMIT_SUB_REG(rd, rn, 2) EMIT_SUB_REG(rd, rn, 3) EMIT_SUB_REG(rd, rn, 4) \
    EMIT_SUB_REG(rd, rn, 5) EMIT_SUB_REG(rd, rn, 6) EMIT_SUB_REG(rd, rn, 7) EMIT_SUB_REG(rd, rn, 8) \
    EMIT_SUB_REG(rd, rn, 9) EMIT_SUB_REG(rd, rn, 10) EMIT_SUB_REG(rd, rn, 11) EMIT_SUB_REG(rd, rn, 12) \
    EMIT_SUB_REG(rd, rn, 13) EMIT_SUB_REG(rd, rn, 14) EMIT_SUB_REG(rd, rn, 15) EMIT_SUB_REG(rd, rn, 16)

#define EMIT_SUB_REG_ROW(rd) \
    EMIT_SUB_REG_RN_RM(rd, 1) EMIT_SUB_REG_RN_RM(rd, 2) EMIT_SUB_REG_RN_RM(rd, 3) EMIT_SUB_REG_RN_RM(rd, 4) \
    EMIT_SUB_REG_RN_RM(rd, 5) EMIT_SUB_REG_RN_RM(rd, 6) EMIT_SUB_REG_RN_RM(rd, 7) EMIT_SUB_REG_RN_RM(rd, 8) \
    EMIT_SUB_REG_RN_RM(rd, 9) EMIT_SUB_REG_RN_RM(rd, 10) EMIT_SUB_REG_RN_RM(rd, 11) EMIT_SUB_REG_RN_RM(rd, 12) \
    EMIT_SUB_REG_RN_RM(rd, 13) EMIT_SUB_REG_RN_RM(rd, 14) EMIT_SUB_REG_RN_RM(rd, 15) EMIT_SUB_REG_RN_RM(rd, 16)

EMIT_SUB_REG_ROW(1) EMIT_SUB_REG_ROW(2) EMIT_SUB_REG_ROW(3) EMIT_SUB_REG_ROW(4)
EMIT_SUB_REG_ROW(5) EMIT_SUB_REG_ROW(6) EMIT_SUB_REG_ROW(7) EMIT_SUB_REG_ROW(8)
EMIT_SUB_REG_ROW(9) EMIT_SUB_REG_ROW(10) EMIT_SUB_REG_ROW(11) EMIT_SUB_REG_ROW(12)
EMIT_SUB_REG_ROW(13) EMIT_SUB_REG_ROW(14) EMIT_SUB_REG_ROW(15) EMIT_SUB_REG_ROW(16)

#define SUB_REG_RN(rd, rn) { \
    gadget_sub_reg_##rd##_##rn##_1, gadget_sub_reg_##rd##_##rn##_2, \
    gadget_sub_reg_##rd##_##rn##_3, gadget_sub_reg_##rd##_##rn##_4, \
    gadget_sub_reg_##rd##_##rn##_5, gadget_sub_reg_##rd##_##rn##_6, \
    gadget_sub_reg_##rd##_##rn##_7, gadget_sub_reg_##rd##_##rn##_8, \
    gadget_sub_reg_##rd##_##rn##_9, gadget_sub_reg_##rd##_##rn##_10, \
    gadget_sub_reg_##rd##_##rn##_11, gadget_sub_reg_##rd##_##rn##_12, \
    gadget_sub_reg_##rd##_##rn##_13, gadget_sub_reg_##rd##_##rn##_14, \
    gadget_sub_reg_##rd##_##rn##_15, gadget_sub_reg_##rd##_##rn##_16 \
}

#define SUB_REG_ROW(rd) { SUB_REG_RN(rd, 1), SUB_REG_RN(rd, 2), SUB_REG_RN(rd, 3), SUB_REG_RN(rd, 4), \
    SUB_REG_RN(rd, 5), SUB_REG_RN(rd, 6), SUB_REG_RN(rd, 7), SUB_REG_RN(rd, 8), \
    SUB_REG_RN(rd, 9), SUB_REG_RN(rd, 10), SUB_REG_RN(rd, 11), SUB_REG_RN(rd, 12), \
    SUB_REG_RN(rd, 13), SUB_REG_RN(rd, 14), SUB_REG_RN(rd, 15), SUB_REG_RN(rd, 16) }

const tcti_gadget_t gadget_sub_reg[16][16][16] = {
    SUB_REG_ROW(1), SUB_REG_ROW(2), SUB_REG_ROW(3), SUB_REG_ROW(4),
    SUB_REG_ROW(5), SUB_REG_ROW(6), SUB_REG_ROW(7), SUB_REG_ROW(8),
    SUB_REG_ROW(9), SUB_REG_ROW(10), SUB_REG_ROW(11), SUB_REG_ROW(12),
    SUB_REG_ROW(13), SUB_REG_ROW(14), SUB_REG_ROW(15), SUB_REG_ROW(16)
};

/*
 * AND/ORR/EOR Register Gadgets
 */

#define EMIT_LOGIC_REG(op, rd, rn, rm) \
    __attribute__((naked)) void gadget_##op##_reg_##rd##_##rn##_##rm(void) \
    { \
        asm volatile( \
            #op " %0, %1, %2\n\t" \
            GADGET_EPILOGUE \
            : : "r"(XREG(rd)), "r"(XREG(rn)), "r"(XREG(rm)) : "x27", "x28" \
        ); \
    }

// Similar pattern for logical ops...
// (omitted for brevity, would follow same macro pattern)

/*
 * NOP Gadget
 */
__attribute__((naked)) void gadget_nop(void)
{
    asm volatile(
        "nop\n\t"
        GADGET_EPILOGUE
        ::: "x27", "x28"
    );
}

/*
 * System Call Gadget
 * On SVC, we need to exit the gadget chain and handle syscall in C
 */
__attribute__((naked)) void gadget_svc(void)
{
    asm volatile(
        // Save x8 (syscall number) and x0-x5 (args) somewhere safe
        // Then return to C code to handle syscall
        "mov x0, #1\n\t"  // Signal: SVC encountered
        "ret\n\t"
        ::: "x0"
    );
}
