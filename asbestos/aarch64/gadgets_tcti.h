#ifndef AARCH64_GADGETS_TCTI_H
#define AARCH64_GADGETS_TCTI_H

/*
 * TCTI (Tiny Code Threaded Interpreter) style gadgets for aarch64
 *
 * Based on UTM's QEMU TCTI approach:
 * - Pre-generated gadgets for all register combinations
 * - Threaded dispatch: each gadget loads next address and branches
 * - No runtime code generation (App Store friendly)
 */

#include "misc.h"
#include "emu/aarch64/cpu.h"

// TCTI register mapping
// Guest x0-x15 mapped to host x1-x16
// x28 = bytecode stream pointer
// x27 = temporary for next gadget address
// x29 = frame pointer (preserved)
// x30 = link register

#define TCTI_GUEST_REGS 16

// Bytecode stream pointer
#define TCTI_IP  x28
#define TCTI_TMP x27

// Guest register base (x1-x16 hold guest x0-x15)
#define TCTI_REG(n) (1 + (n))

// Epilogue: load next gadget and jump to it
// Each gadget ends with this
#define TCTI_EPILOGUE \
    "ldr x27, [x28], #8\n\t" \
    "br x27\n\t"

// Gadget function type
typedef void (*tcti_gadget_t)(void);

// Block structure: array of gadget addresses
typedef struct tcti_block {
    tcti_gadget_t *gadgets;     // Array of gadget function pointers
    uint64_t guest_addr;        // Starting guest address
    size_t count;               // Number of gadgets
    struct tcti_block *next;    // Hash collision chain
} tcti_block_t;

// Pre-generated gadget collections

// ADD immediate: Rd = Rn + imm (no shift)
// 16 destinations * 16 sources = 256 gadgets
extern const tcti_gadget_t gadget_add_imm[TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// ADD register: Rd = Rn + Rm
// 16 * 16 * 16 = 4096 gadgets
extern const tcti_gadget_t gadget_add_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// SUB immediate
extern const tcti_gadget_t gadget_sub_imm[TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// SUB register
extern const tcti_gadget_t gadget_sub_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// MOV register: Rd = Rn
extern const tcti_gadget_t gadget_mov_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// MOV immediate: Rd = imm32
extern const tcti_gadget_t gadget_mov_imm[TCTI_GUEST_REGS];

// MOV immediate64: Rd = imm64
extern const tcti_gadget_t gadget_mov_imm64[TCTI_GUEST_REGS];

// AND/ORR/EOR register
extern const tcti_gadget_t gadget_and_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];
extern const tcti_gadget_t gadget_orr_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];
extern const tcti_gadget_t gadget_eor_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// Shift: LSL, LSR, ASR
extern const tcti_gadget_t gadget_lsl_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];
extern const tcti_gadget_t gadget_lsr_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];
extern const tcti_gadget_t gadget_asr_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// Multiply
extern const tcti_gadget_t gadget_mul_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// Divide (signed and unsigned)
extern const tcti_gadget_t gadget_sdiv_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];
extern const tcti_gadget_t gadget_udiv_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS][TCTI_GUEST_REGS];

// Compare: CMP (sets flags, no result)
extern const tcti_gadget_t gadget_cmp_reg[TCTI_GUEST_REGS][TCTI_GUEST_REGS];
extern const tcti_gadget_t gadget_cmp_imm[TCTI_GUEST_REGS];

// Conditional branch: B.cond target
// Not pre-generated - handled specially
extern void gadget_bcond(int cond, uint64_t target);

// Unconditional branch
extern void gadget_b(uint64_t target);
extern void gadget_bl(uint64_t target);
extern void gadget_br(int Rn);
extern void gadget_blr(int Rn);

// Compare and branch
extern void gadget_cbz(int Rt, uint64_t target);
extern void gadget_cbnz(int Rt, uint64_t target);

// Load/Store with immediate offset
// These need TLB translation - call into C helper
extern void gadget_ldr(int Rt, int Rn, int64_t offset);
extern void gadget_ldrb(int Rt, int Rn, int64_t offset);
extern void gadget_ldrh(int Rt, int Rn, int64_t offset);
extern void gadget_ldrsw(int Rt, int Rn, int64_t offset);

extern void gadget_str(int Rt, int Rn, int64_t offset);
extern void gadget_strb(int Rt, int Rn, int64_t offset);
extern void gadget_strh(int Rt, int Rn, int64_t offset);

// System instructions
extern void gadget_svc(uint16_t imm);  // System call
extern void gadget_mrs(int Rt, uint32_t sysreg);
extern void gadget_msr(uint32_t sysreg, int Rt);

// Barriers
extern void gadget_dmb(void);
extern void gadget_dsb(void);
extern void gadget_isb(void);

// NOP
extern void gadget_nop(void);

// Block entry/exit
extern void tcti_enter_block(tcti_block_t *block);
extern void tcti_exit_block(int reason);

// Block cache
tcti_block_t *tcti_lookup_block(uint64_t guest_addr);
tcti_block_t *tcti_compile_block(uint64_t guest_addr);
void tcti_invalidate_block(uint64_t guest_addr);

#endif
