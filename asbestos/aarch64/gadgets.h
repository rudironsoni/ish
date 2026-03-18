#ifndef AARCH64_GADGETS_H
#define AARCH64_GADGETS_H

#include "emu/aarch64/cpu.h"

// Gadget function type - takes CPU state, returns next gadget address
typedef void (*gadget_fn_t)(struct cpu_state *cpu);

// Core execution structures
typedef struct a64_fiber_block {
    void **code;           // Array of gadget function pointers
    uint64_t guest_addr;   // Guest virtual address of block start
    size_t num_gadgets;    // Number of gadgets in block
    struct a64_fiber_block *next;  // For hash table
} a64_fiber_block_t;

// Gadget entry/exit
gadget_fn_t a64_gadget_entry(struct cpu_state *cpu, uint64_t addr);
void a64_gadget_exit(struct cpu_state *cpu, int status);

// Core arithmetic gadgets
gadget_fn_t gadget_add_imm(struct cpu_state *cpu);
gadget_fn_t gadget_add_reg(struct cpu_state *cpu);
gadget_fn_t gadget_sub_imm(struct cpu_state *cpu);
gadget_fn_t gadget_sub_reg(struct cpu_state *cpu);

// Logical gadgets
gadget_fn_t gadget_and_imm(struct cpu_state *cpu);
gadget_fn_t gadget_and_reg(struct cpu_state *cpu);
gadget_fn_t gadget_orr_imm(struct cpu_state *cpu);
gadget_fn_t gadget_orr_reg(struct cpu_state *cpu);
gadget_fn_t gadget_eor_imm(struct cpu_state *cpu);
gadget_fn_t gadget_eor_reg(struct cpu_state *cpu);

// Move gadgets
gadget_fn_t gadget_mov_reg(struct cpu_state *cpu);
gadget_fn_t gadget_mov_imm(struct cpu_state *cpu);
gadget_fn_t gadget_mvn_reg(struct cpu_state *cpu);

// Shift gadgets
gadget_fn_t gadget_lsl_imm(struct cpu_state *cpu);
gadget_fn_t gadget_lsr_imm(struct cpu_state *cpu);
gadget_fn_t gadget_asr_imm(struct cpu_state *cpu);

// Comparison gadgets
gadget_fn_t gadget_cmp_imm(struct cpu_state *cpu);
gadget_fn_t gadget_cmp_reg(struct cpu_state *cpu);
gadget_fn_t gadget_tst_imm(struct cpu_state *cpu);
gadget_fn_t gadget_tst_reg(struct cpu_state *cpu);

// Branch gadgets
gadget_fn_t gadget_b(struct cpu_state *cpu);
gadget_fn_t gadget_b_cond(struct cpu_state *cpu);
gadget_fn_t gadget_bl(struct cpu_state *cpu);
gadget_fn_t gadget_br(struct cpu_state *cpu);
gadget_fn_t gadget_blr(struct cpu_state *cpu);
gadget_fn_t gadget_ret(struct cpu_state *cpu);
gadget_fn_t gadget_cbz(struct cpu_state *cpu);
gadget_fn_t gadget_cbnz(struct cpu_state *cpu);

// Load/store gadgets
gadget_fn_t gadget_ldr_imm(struct cpu_state *cpu);
gadget_fn_t gadget_ldr_reg(struct cpu_state *cpu);
gadget_fn_t gadget_ldrb_imm(struct cpu_state *cpu);
gadget_fn_t gadget_ldrh_imm(struct cpu_state *cpu);
gadget_fn_t gadget_ldrsw_imm(struct cpu_state *cpu);

gadget_fn_t gadget_str_imm(struct cpu_state *cpu);
gadget_fn_t gadget_str_reg(struct cpu_state *cpu);
gadget_fn_t gadget_strb_imm(struct cpu_state *cpu);
gadget_fn_t gadget_strh_imm(struct cpu_state *cpu);

gadget_fn_t gadget_ldp(struct cpu_state *cpu);
gadget_fn_t gadget_stp(struct cpu_state *cpu);

// System gadgets
gadget_fn_t gadget_svc(struct cpu_state *cpu);
gadget_fn_t gadget_mrs(struct cpu_state *cpu);
gadget_fn_t gadget_msr(struct cpu_state *cpu);
gadget_fn_t gadget_isb(struct cpu_state *cpu);
gadget_fn_t gadget_dsb(struct cpu_state *cpu);
gadget_fn_t gadget_dmb(struct cpu_state *cpu);
gadget_fn_t gadget_nop(struct cpu_state *cpu);

// Floating point gadgets
gadget_fn_t gadget_fadd(struct cpu_state *cpu);
gadget_fn_t gadget_fsub(struct cpu_state *cpu);
gadget_fn_t gadget_fmul(struct cpu_state *cpu);
gadget_fn_t gadget_fdiv(struct cpu_state *cpu);
gadget_fn_t gadget_fcmp(struct cpu_state *cpu);

// Helper macros for flag calculations
#define A64_SET_NZCV(cpu, result, is_64bit) do { \
    (cpu)->z = ((result) == 0); \
    (cpu)->n = is_64bit ? (((result) >> 63) & 1) : (((result) >> 31) & 1); \
} while(0)

#define A64_SET_CARRY(cpu, carry) do { \
    (cpu)->c = (carry); \
} while(0)

#define A64_SET_OVERFLOW(cpu, overflow) do { \
    (cpu)->v = (overflow); \
} while(0)

// Memory access helpers (with TLB translation)
int a64_read_mem8(struct cpu_state *cpu, uint64_t addr, uint8_t *val);
int a64_read_mem16(struct cpu_state *cpu, uint64_t addr, uint16_t *val);
int a64_read_mem32(struct cpu_state *cpu, uint64_t addr, uint32_t *val);
int a64_read_mem64(struct cpu_state *cpu, uint64_t addr, uint64_t *val);

int a64_write_mem8(struct cpu_state *cpu, uint64_t addr, uint8_t val);
int a64_write_mem16(struct cpu_state *cpu, uint64_t addr, uint16_t val);
int a64_write_mem32(struct cpu_state *cpu, uint64_t addr, uint32_t val);
int a64_write_mem64(struct cpu_state *cpu, uint64_t addr, uint64_t val);

#endif
