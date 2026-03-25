/*
 * Memory access gadgets for TCTI
 *
 * Memory-backed registers (x16-x30, SP) are stored in cpu_state memory.
 * To operate on them, we:
 *   1. Load into temp register (x14 for values, x15 for auxiliaries)
 *   2. Execute operation
 *   3. Store back to memory
 *
 * Register mapping:
 *   x0-x15 (guest)  -> x1-x16 (host)   [TCTI-mapped, always hot]
 *   x16-x30 (guest) -> memory only     [load/store via gadgets]
 *   SP (guest)      -> memory only     [load/store via gadgets]
 *   x14-x15 (host)  -> temps           [for memory-backed register ops]
 *   x27-x29 (host)  -> TCTI internals  [gadget ptr, bytecode, cpu_state]
 */

#include "gadgets_tcti.h"
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/memory.h"
#include <stddef.h>

// Verify offset assumptions at compile time
#define XREG_OFFSET(n) (offsetof(struct cpu_state, x[n]))
#define SP_OFFSET offsetof(struct cpu_state, sp)
#define PC_OFFSET offsetof(struct cpu_state, pc)
#define PSTATE_OFFSET offsetof(struct cpu_state, pstate)

// Guest x[0] is at offset 16 (after mmu pointer and cycle counter)
static_assert(XREG_OFFSET(0) == 16, "x[0] offset check");
static_assert(SP_OFFSET == 264, "SP offset check");
static_assert(PC_OFFSET == 272, "pc offset check");
static_assert(PSTATE_OFFSET == 280, "pstate offset check");

#define REG_OFFSET(n) (XREG_OFFSET(n))

static uint64_t tcti_read_reg_or_sp(struct cpu_state *cpu, int reg) {
    if (reg == 31)
        return cpu->sp;
    if (reg < 0 || reg > 30)
        return 0;
    return cpu->x[reg];
}

static void tcti_write_reg_or_sp(struct cpu_state *cpu, int reg, uint64_t value, int is_64bit) {
    uint64_t masked = is_64bit ? value : (uint32_t) value;
    if (reg == 31) {
        cpu->sp = masked;
        return;
    }
    if (reg < 0 || reg > 30)
        return;
    cpu->x[reg] = masked;
}

static uint64_t tcti_extend_ldst_offset(struct cpu_state *cpu, int rm, int extend_type) {
    uint64_t value = tcti_read_reg_or_sp(cpu, rm);

    switch (extend_type) {
        case A64_EXT_UXTW:
            return (uint32_t) value;
        case A64_EXT_SXTW:
            return (uint64_t) (int64_t) (int32_t) value;
        case A64_EXT_SXTX:
            return (uint64_t) (int64_t) value;
        case A64_EXT_UXTX:
        case A64_EXT_LSL:
        default:
            return value;
    }
}

static int a64_tcti_ldst_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
        uint64_t rn, int64_t imm, uint64_t size, uint64_t idx_mode,
        uint64_t meta, uint64_t is_load) {
    uint64_t base = tcti_read_reg_or_sp(cpu, (int) rn);
    uint64_t addr = base;
    uint64_t raw_offset = 0;
    uint64_t is_signed = meta & 0xff;
    uint64_t is_reg_offset = (meta >> 8) & 0xff;
    int rm = (meta >> 16) & 0xff;
    int extend_type = (meta >> 24) & 0xff;
    int reg_shift = (meta >> 32) & 0xff;
    int width;

    // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] %s pc=0x%llx rt=%llu rn=%llu base=0x%llx imm=%lld size=%llu idx=%llu signed=%llu regoff=%llu rm=%d ext=%d sh=%d\n",
    // [DIAGNOSTIC DISABLED]         is_load ? "LDR" : "STR",
    // [DIAGNOSTIC DISABLED]         (unsigned long long) fault_pc,
    // [DIAGNOSTIC DISABLED]         (unsigned long long) rt,
    // [DIAGNOSTIC DISABLED]         (unsigned long long) rn,
    // [DIAGNOSTIC DISABLED]         (unsigned long long) base,
    // [DIAGNOSTIC DISABLED]         (long long) imm,
    // [DIAGNOSTIC DISABLED]         (unsigned long long) size,
    // [DIAGNOSTIC DISABLED]         (unsigned long long) idx_mode,
    // [DIAGNOSTIC DISABLED]         (unsigned long long) is_signed,
    // [DIAGNOSTIC DISABLED]         (unsigned long long) is_reg_offset,
    // [DIAGNOSTIC DISABLED]         rm,
    // [DIAGNOSTIC DISABLED]         extend_type,
    // [DIAGNOSTIC DISABLED]         reg_shift);

    if (is_reg_offset) {
        uint64_t offset = tcti_extend_ldst_offset(cpu, rm, extend_type);
        raw_offset = tcti_read_reg_or_sp(cpu, rm);
        // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] regoff raw=0x%llx extval=0x%llx\n",
        // [DIAGNOSTIC DISABLED]         (unsigned long long) raw_offset,
        // [DIAGNOSTIC DISABLED]         (unsigned long long) offset);
        addr = base + (offset << reg_shift);
    } else {
        switch (idx_mode) {
            case A64_PRE_INDEX:
                base += imm;
                addr = base;
                break;
            case A64_POST_INDEX:
                addr = base;
                break;
            case A64_INDEX_OFFSET:
            default:
                addr = base + imm;
                break;
        }
    }

    // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] effective addr=0x%llx\n", (unsigned long long) addr);

    switch (size) {
        case A64_SIZE_B: width = 1; break;
        case A64_SIZE_H: width = 2; break;
        case A64_SIZE_W: width = 4; break;
        case A64_SIZE_X: width = 8; break;
        default: return TCTI_EXIT_FAULT;
    }

    if (is_load) {
        uint64_t value = 0;
        int mem_ret;

        switch (width) {
            case 1: {
                uint8_t tmp;
                mem_ret = a64_guest_read8(cpu, cpu->tlb, addr, &tmp);
                value = is_signed ? (uint64_t) (int64_t) (int8_t) tmp : tmp;
                break;
            }
            case 2: {
                uint16_t tmp;
                mem_ret = a64_guest_read16(cpu, cpu->tlb, addr, &tmp);
                value = is_signed ? (uint64_t) (int64_t) (int16_t) tmp : tmp;
                break;
            }
            case 4: {
                uint32_t tmp;
                mem_ret = a64_guest_read32(cpu, cpu->tlb, addr, &tmp);
                value = is_signed ? (uint64_t) (int64_t) (int32_t) tmp : tmp;
                break;
            }
            case 8: {
                uint64_t tmp;
                mem_ret = a64_guest_read64(cpu, cpu->tlb, addr, &tmp);
                value = tmp;
                break;
            }
            default:
                mem_ret = A64_MEM_FAULT;
                break;
        }

        if (mem_ret != A64_MEM_OK) {
            // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] load fault addr=0x%llx fault_addr=0x%llx\n",
            // [DIAGNOSTIC DISABLED]         (unsigned long long) addr,
            // [DIAGNOSTIC DISABLED]         (unsigned long long) cpu->fault_addr);
            cpu->pc = fault_pc;
            cpu->fault_was_write = false;
            return TCTI_EXIT_FAULT;
        }

        tcti_write_reg_or_sp(cpu, (int) rt, value, size == A64_SIZE_X);
        // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] load value=0x%llx -> r%llu\n",
        // [DIAGNOSTIC DISABLED]         (unsigned long long) value,
        // [DIAGNOSTIC DISABLED]         (unsigned long long) rt);
    } else {
        uint64_t value = tcti_read_reg_or_sp(cpu, (int) rt);
        int mem_ret;

        // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] store value=0x%llx from r%llu\n",
        // [DIAGNOSTIC DISABLED]         (unsigned long long) value,
        // [DIAGNOSTIC DISABLED]         (unsigned long long) rt);

        switch (width) {
            case 1: mem_ret = a64_guest_write8(cpu, cpu->tlb, addr, (uint8_t) value); break;
            case 2: mem_ret = a64_guest_write16(cpu, cpu->tlb, addr, (uint16_t) value); break;
            case 4: mem_ret = a64_guest_write32(cpu, cpu->tlb, addr, (uint32_t) value); break;
            case 8: mem_ret = a64_guest_write64(cpu, cpu->tlb, addr, value); break;
            default: mem_ret = A64_MEM_FAULT; break;
        }

        if (mem_ret != A64_MEM_OK) {
            // [DIAGNOSTIC DISABLED] static int store_fault_count = 0;
            // [DIAGNOSTIC DISABLED] store_fault_count++;
            // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] store fault #%d addr=0x%llx fault_addr=0x%llx pc=0x%llx\n",
            // [DIAGNOSTIC DISABLED]         store_fault_count,
            // [DIAGNOSTIC DISABLED]         (unsigned long long) addr,
            // [DIAGNOSTIC DISABLED]         (unsigned long long) cpu->fault_addr,
            // [DIAGNOSTIC DISABLED]         (unsigned long long) fault_pc);
            cpu->pc = fault_pc;
            cpu->fault_was_write = true;
            return TCTI_EXIT_FAULT;
        }
    }

    if (!is_reg_offset && (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX)) {
        uint64_t updated = (idx_mode == A64_POST_INDEX) ? (base + imm) : base;
        tcti_write_reg_or_sp(cpu, (int) rn, updated, true);
        // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] writeback rn=%llu value=0x%llx\n",
        // [DIAGNOSTIC DISABLED]         (unsigned long long) rn,
        // [DIAGNOSTIC DISABLED]         (unsigned long long) updated);
    }

    // [DIAGNOSTIC DISABLED] printk("[TCTI-LDST] success\n");
    return TCTI_EXIT_NORMAL;
}

int a64_tcti_ldr_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
        uint64_t rn, int64_t imm, uint64_t size, uint64_t idx_mode,
        uint64_t meta) {
    return a64_tcti_ldst_helper(cpu, fault_pc, rt, rn, imm, size, idx_mode,
            meta, 1);
}

int a64_tcti_str_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
        uint64_t rn, int64_t imm, uint64_t size, uint64_t idx_mode,
        uint64_t meta) {
    return a64_tcti_ldst_helper(cpu, fault_pc, rt, rn, imm, size, idx_mode,
            meta, 0);
}

// ============================================================================
// Exit Gadget - Marks end of gadget stream
// ============================================================================

__attribute__((naked)) void gadget_exit_impl(void) {
    asm volatile(
        "mov x0, #0\n\t"               // Exit reason = normal
        "b _tcti_exit_block\n\t"       // Jump to exit handler
    );
}

tcti_gadget_t gadget_exit = gadget_exit_impl;

// ============================================================================
// Memory-backed register load/store gadgets
//
// Each gadget:
//   - Loads/stores one guest register to/from temp x14
//   - Advances to next gadget via x28 bytecode pointer
//   - Branches to next gadget
//
// Index mapping: 0=x16, 1=x17, ..., 14=x30 (15 entries)
// ============================================================================

// Load guest x[16 + idx] into host temp x14
#define GEN_LOAD_XREG(idx) \
    __attribute__((naked)) void gadget_load_x##idx##_impl(void) { \
        asm volatile( \
            "ldr x14, [x29, %[off]]\n\t" \
            "ldr x27, [x28], #8\n\t" \
            "br x27\n\t" \
            : \
            : [off] "i" (XREG_OFFSET(16 + idx)) \
        ); \
    }

// Store host temp x14 to guest x[16 + idx]
#define GEN_STORE_XREG(idx) \
    __attribute__((naked)) void gadget_store_x##idx##_impl(void) { \
        asm volatile( \
            "str x14, [x29, %[off]]\n\t" \
            "ldr x27, [x28], #8\n\t" \
            "br x27\n\t" \
            : \
            : [off] "i" (XREG_OFFSET(16 + idx)) \
        ); \
    }

// Generate load gadgets for x16-x30 (indices 0-14)
GEN_LOAD_XREG(0)   // x16
GEN_LOAD_XREG(1)   // x17
GEN_LOAD_XREG(2)   // x13
GEN_LOAD_XREG(3)   // x19
GEN_LOAD_XREG(4)   // x20
GEN_LOAD_XREG(5)   // x21
GEN_LOAD_XREG(6)   // x22
GEN_LOAD_XREG(7)   // x23
GEN_LOAD_XREG(8)   // x24
GEN_LOAD_XREG(9)   // x25
GEN_LOAD_XREG(10)  // x26
GEN_LOAD_XREG(11)  // x27
GEN_LOAD_XREG(12)  // x28
GEN_LOAD_XREG(13)  // x29
GEN_LOAD_XREG(14)  // x30

// Generate store gadgets for x16-x30 (indices 0-14)
GEN_STORE_XREG(0)   // x16
GEN_STORE_XREG(1)   // x17
GEN_STORE_XREG(2)   // x13
GEN_STORE_XREG(3)   // x19
GEN_STORE_XREG(4)   // x20
GEN_STORE_XREG(5)   // x21
GEN_STORE_XREG(6)   // x22
GEN_STORE_XREG(7)   // x23
GEN_STORE_XREG(8)   // x24
GEN_STORE_XREG(9)   // x25
GEN_STORE_XREG(10)  // x26
GEN_STORE_XREG(11)  // x27
GEN_STORE_XREG(12)  // x28
GEN_STORE_XREG(13)  // x29
GEN_STORE_XREG(14)  // x30

// x30 (LR) load/store uses the same x14 temp contract as the table gadgets
__attribute__((naked)) void gadget_load_x30_impl(void) {
    asm volatile(
        "ldr x14, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (XREG_OFFSET(30))
    );
}

__attribute__((naked)) void gadget_store_x30_impl(void) {
    asm volatile(
        "str x14, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (XREG_OFFSET(30))
    );
}

// SP load/store - uses x14 as temp to match gen.c temp conventions
// These are the actual implementations matching header declarations
__attribute__((naked)) void gadget_load_sp(void) {
    asm volatile(
        "ldr x14, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (SP_OFFSET)
    );
}

__attribute__((naked)) void gadget_store_sp(void) {
    asm volatile(
        "str x14, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (SP_OFFSET)
    );
}

// ============================================================================
// Function pointer tables for load/store
//
// These are indexed by (guest_reg - 16) for x16-x30
// ============================================================================

// Load table: index 0=x16, 14=x30
const tcti_gadget_t gadget_load_xreg_16_to_30[15] = {
    gadget_load_x0_impl,   // x16
    gadget_load_x1_impl,   // x17
    gadget_load_x2_impl,   // x13
    gadget_load_x3_impl,   // x19
    gadget_load_x4_impl,   // x20
    gadget_load_x5_impl,   // x21
    gadget_load_x6_impl,   // x22
    gadget_load_x7_impl,   // x23
    gadget_load_x8_impl,   // x24
    gadget_load_x9_impl,   // x25
    gadget_load_x10_impl,  // x26
    gadget_load_x11_impl,  // x27
    gadget_load_x12_impl,  // x28
    gadget_load_x13_impl,  // x29
    gadget_load_x14_impl,  // x30
};

// Store table: index 0=x16, 14=x30
const tcti_gadget_t gadget_store_xreg_16_to_30[15] = {
    gadget_store_x0_impl,   // x16
    gadget_store_x1_impl,   // x17
    gadget_store_x2_impl,   // x13
    gadget_store_x3_impl,   // x19
    gadget_store_x4_impl,   // x20
    gadget_store_x5_impl,   // x21
    gadget_store_x6_impl,   // x22
    gadget_store_x7_impl,   // x23
    gadget_store_x8_impl,   // x24
    gadget_store_x9_impl,   // x25
    gadget_store_x10_impl,  // x26
    gadget_store_x11_impl,  // x27
    gadget_store_x12_impl,  // x28
    gadget_store_x13_impl,  // x29
    gadget_store_x14_impl,  // x30
};

// SP accessors - these match the header declarations as function prototypes
// The actual implementations are gadget_load_sp_impl and gadget_store_sp_impl
// which are naked functions, not variables

// ============================================================================
// TCTI Load/Store Gadgets for memory access
// ============================================================================
//
// These gadgets perform actual memory loads/stores through the TLB.
// They use x16 as the address temp and x17 as the data temp.

// Load 64-bit value from memory[Rn + offset] into x17
// Address calculation: x29 (cpu) + x[n]*8 + 16 (x[0] offset)
__attribute__((naked)) void gadget_tcti_ldr_64(void) {
    asm volatile(
        // x0 contains the base address (from Rn gadget)
        // Load from [x0] into x17
        "ldr x17, [x0]\n\t"
        // Move to destination register (handled by caller)
        // For now, keep in x17 and chain
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// Store 64-bit value from x17 to memory[Rn + offset]
__attribute__((naked)) void gadget_tcti_str_64(void) {
    asm volatile(
        // x0 contains the base address
        // Store x17 to [x0]
        "str x17, [x0]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// Compute address: x29 + offset + (Rn * 8) for guest register access
// This loads the guest register value from cpu_state
__attribute__((naked)) void gadget_tcti_compute_reg_addr(void) {
    asm volatile(
        // Rn index is in bytecode (next 8 bytes)
        "ldr x16, [x28], #8\n\t"     // Load Rn index
        // Compute: x29 + 16 + (x16 * 8)
        "lsl x16, x16, #3\n\t"       // x16 = Rn * 8
        "add x16, x16, #16\n\t"      // x16 = Rn*8 + 16
        "add x0, x29, x16\n\t"       // x0 = cpu + offset = &x[Rn]
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// Compute address: x29 + offset for immediate offset
__attribute__((naked)) void gadget_tcti_compute_imm_addr(void) {
    asm volatile(
        // Offset is in bytecode (next 8 bytes)
        "ldr x16, [x28], #8\n\t"     // Load offset
        "add x0, x29, x16\n\t"       // x0 = cpu + offset
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

tcti_gadget_t gadget_tcti_ldr = gadget_tcti_ldr_64;
tcti_gadget_t gadget_tcti_str = gadget_tcti_str_64;
tcti_gadget_t gadget_tcti_compute_reg_addr_fn = gadget_tcti_compute_reg_addr;
tcti_gadget_t gadget_tcti_compute_imm_addr_fn = gadget_tcti_compute_imm_addr;

// ============================================================================
// Helper functions for C code to access memory-backed registers
// ============================================================================

uint64_t a64_read_xreg(struct cpu_state *cpu, int reg) {
    if (reg >= 0 && reg < 31)
        return cpu->x[reg];
    return 0;
}

void a64_write_xreg(struct cpu_state *cpu, int reg, uint64_t val) {
    if (reg >= 0 && reg < 31)
        cpu->x[reg] = val;
}

uint64_t a64_read_sp(struct cpu_state *cpu) {
    return cpu->sp;
}

void a64_write_sp(struct cpu_state *cpu, uint64_t val) {
    cpu->sp = val;
}

/* ============================================================================
 * Complex Gadgets (moved from separate file to be included in build)
 * ============================================================================ */

__attribute__((naked)) void gadget_b_impl(void) {
    asm volatile(
        "ldr x0, [x28], #8\n\t"
        "str x0, [x29, #272]\n\t"
        "mov x0, #0\n\t"
        "b _tcti_exit_block\n\t"
    );
}

tcti_gadget_t gadget_b = gadget_b_impl;

#define GEN_BCOND(name, cond) \
    __attribute__((naked)) void gadget_bcond_##name##_impl(void) { \
        asm volatile( \
            "ldr x16, [x28], #8\n\t" \
            "ldr x17, [x28], #8\n\t" \
            "b." #cond " 1f\n\t" \
            "mov x16, x17\n\t" \
            "1:\n\t" \
            "str x16, [x29, %[pc_off]]\n\t" \
            "mov x0, #0\n\t" \
            "b _tcti_exit_block\n\t" \
            : \
            : [pc_off] "i" (PC_OFFSET) \
        ); \
    }

GEN_BCOND(eq, eq);
GEN_BCOND(ne, ne);
GEN_BCOND(cs, cs);
GEN_BCOND(cc, cc);
GEN_BCOND(mi, mi);
GEN_BCOND(pl, pl);
GEN_BCOND(vs, vs);
GEN_BCOND(vc, vc);
GEN_BCOND(hi, hi);
GEN_BCOND(ls, ls);
GEN_BCOND(ge, ge);
GEN_BCOND(lt, lt);
GEN_BCOND(gt, gt);
GEN_BCOND(le, le);
GEN_BCOND(al, al);
GEN_BCOND(nv, al);

const tcti_gadget_t gadget_bcond[16] = {
    gadget_bcond_eq_impl,
    gadget_bcond_ne_impl,
    gadget_bcond_cs_impl,
    gadget_bcond_cc_impl,
    gadget_bcond_mi_impl,
    gadget_bcond_pl_impl,
    gadget_bcond_vs_impl,
    gadget_bcond_vc_impl,
    gadget_bcond_hi_impl,
    gadget_bcond_ls_impl,
    gadget_bcond_ge_impl,
    gadget_bcond_lt_impl,
    gadget_bcond_gt_impl,
    gadget_bcond_le_impl,
    gadget_bcond_al_impl,
    gadget_bcond_nv_impl,
};

__attribute__((naked)) void gadget_br_impl(void) {
    asm volatile(
        "ldr x0, [x28], #8\n\t"
        "cmp x0, #31\n\t"
        "b.eq 1f\n\t"
        "add x1, x29, #16\n\t"
        "lsl x2, x0, #3\n\t"
        "ldr x0, [x1, x2]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "ldr x0, [x29, #264]\n\t"
        "2:\n\t"
        "str x0, [x29, #272]\n\t"
        "mov x0, #0\n\t"
        "b _tcti_exit_block\n\t"
    );
}

tcti_gadget_t gadget_br = gadget_br_impl;

#define GEN_CBZ_TABLE(kind, mnemonic, hostreg, idx) \
    __attribute__((naked)) void gadget_##kind##_##idx##_impl(void) { \
        asm volatile( \
            "ldr x16, [x28], #8\n\t" \
            "ldr x17, [x28], #8\n\t" \
            mnemonic " x" #hostreg ", 1f\n\t" \
            "mov x16, x17\n\t" \
            "1:\n\t" \
            "str x16, [x29, %[pc_off]]\n\t" \
            "mov x0, #0\n\t" \
            "b _tcti_exit_block\n\t" \
            : \
            : [pc_off] "i" (PC_OFFSET) \
        ); \
    }

GEN_CBZ_TABLE(cbz_reg, "cbz", 1, 0);
GEN_CBZ_TABLE(cbz_reg, "cbz", 2, 1);
GEN_CBZ_TABLE(cbz_reg, "cbz", 3, 2);
GEN_CBZ_TABLE(cbz_reg, "cbz", 4, 3);
GEN_CBZ_TABLE(cbz_reg, "cbz", 5, 4);
GEN_CBZ_TABLE(cbz_reg, "cbz", 6, 5);
GEN_CBZ_TABLE(cbz_reg, "cbz", 7, 6);
GEN_CBZ_TABLE(cbz_reg, "cbz", 8, 7);
GEN_CBZ_TABLE(cbz_reg, "cbz", 9, 8);
GEN_CBZ_TABLE(cbz_reg, "cbz", 10, 9);
GEN_CBZ_TABLE(cbz_reg, "cbz", 11, 10);
GEN_CBZ_TABLE(cbz_reg, "cbz", 12, 11);
GEN_CBZ_TABLE(cbz_reg, "cbz", 13, 12);
GEN_CBZ_TABLE(cbz_reg, "cbz", 14, 13);
GEN_CBZ_TABLE(cbz_reg, "cbz", 15, 14);
GEN_CBZ_TABLE(cbz_reg, "cbz", 16, 15);

GEN_CBZ_TABLE(cbnz_reg, "cbnz", 1, 0);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 2, 1);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 3, 2);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 4, 3);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 5, 4);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 6, 5);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 7, 6);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 8, 7);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 9, 8);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 10, 9);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 11, 10);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 12, 11);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 13, 12);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 14, 13);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 15, 14);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 16, 15);

const tcti_gadget_t gadget_cbz_reg[16] = {
    gadget_cbz_reg_0_impl, gadget_cbz_reg_1_impl, gadget_cbz_reg_2_impl, gadget_cbz_reg_3_impl,
    gadget_cbz_reg_4_impl, gadget_cbz_reg_5_impl, gadget_cbz_reg_6_impl, gadget_cbz_reg_7_impl,
    gadget_cbz_reg_8_impl, gadget_cbz_reg_9_impl, gadget_cbz_reg_10_impl, gadget_cbz_reg_11_impl,
    gadget_cbz_reg_12_impl, gadget_cbz_reg_13_impl, gadget_cbz_reg_14_impl, gadget_cbz_reg_15_impl,
};

const tcti_gadget_t gadget_cbnz_reg[16] = {
    gadget_cbnz_reg_0_impl, gadget_cbnz_reg_1_impl, gadget_cbnz_reg_2_impl, gadget_cbnz_reg_3_impl,
    gadget_cbnz_reg_4_impl, gadget_cbnz_reg_5_impl, gadget_cbnz_reg_6_impl, gadget_cbnz_reg_7_impl,
    gadget_cbnz_reg_8_impl, gadget_cbnz_reg_9_impl, gadget_cbnz_reg_10_impl, gadget_cbnz_reg_11_impl,
    gadget_cbnz_reg_12_impl, gadget_cbnz_reg_13_impl, gadget_cbnz_reg_14_impl, gadget_cbnz_reg_15_impl,
};

__attribute__((naked)) void gadget_sbfm_impl(void) {
    asm volatile(
        // Save guest registers (x0-x15 in guest = x1-x16 in host) to cpu_state
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        // Load parameters (x19-x24 are preserved by ABI, safe to use)
        "ldr x19, [x28], #8\n\t"  // fault_pc
        "ldr x20, [x28], #8\n\t"  // rd
        "ldr x21, [x28], #8\n\t"  // rn
        "ldr x22, [x28], #8\n\t"  // immr
        "ldr x23, [x28], #8\n\t"  // imms
        "ldr x24, [x28], #8\n\t"  // is_64bit
        // Load source operand (using x17 as temp, preserved by TCTI entry)
        "cmp x21, #31\n\t"
        "b.eq 1f\n\t"
        "add x17, x29, #16\n\t"
        "add x17, x17, x21, lsl #3\n\t"
        "ldr x17, [x17]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "ldr x17, [x29, #264]\n\t"  // SP
        "2:\n\t"
        // Compute datasize_mask (x25)
        "mov x0, #0xffffffff\n\t"
        "mov x25, #-1\n\t"
        "cmp x24, #0\n\t"
        "csel x25, x0, x25, eq\n\t"
        "and x17, x17, x25\n\t"       // src &= datasize_mask
        // Compute datasize (x26)
        "cmp x24, #0\n\t"
        "mov x0, #32\n\t"
        "mov x26, #64\n\t"
        "csel x26, x0, x26, eq\n\t"
        // ROR(src, immr) - result in x17
        // Use x19 as temp since params are already loaded
        "sub x19, x26, x22\n\t"       // x19 = datasize - immr (shift amount)
        "lsr x0, x17, x22\n\t"        // x0 = src >> immr
        "lsl x19, x17, x19\n\t"       // x19 = src << (datasize - immr)
        "orr x17, x19, x0\n\t"        // x17 = ROR result
        "and x17, x17, x25\n\t"       // mask to datasize
        // Compute width = (imms - immr + 1) mod datasize
        "cmp x23, x22\n\t"
        "b.hs 3f\n\t"
        "add x0, x23, #1\n\t"
        "add x0, x0, x26\n\t"
        "sub x0, x0, x22\n\t"
        "b 4f\n\t"
        "3:\n\t"
        "sub x0, x23, x22\n\t"
        "add x0, x0, #1\n\t"
        "4:\n\t"
        // Compute wmask = (1 << width) - 1, or all 1s if width == datasize
        "cmp x0, x26\n\t"
        "b.eq 5f\n\t"
        "mov x25, #1\n\t"
        "lsl x25, x25, x0\n\t"
        "sub x25, x25, #1\n\t"
        "b 6f\n\t"
        "5:\n\t"
        "mov x25, #-1\n\t"
        "6:\n\t"
        "and x17, x17, x25\n\t"       // result = ROR & wmask
        // Sign extend: if result[width-1] == 1, set bits [datasize-1:width]
        "sub x0, x0, #1\n\t"          // x0 = width - 1 (MSB position)
        "mov x26, #1\n\t"
        "lsl x26, x26, x0\n\t"        // x26 = 1 << (width-1)
        "tst x17, x26\n\t"            // Test MSB
        "b.eq 9f\n\t"                  // MSB = 0, no sign extension
        "sub x26, x26, #1\n\t"        // x26 = (1 << (width-1)) - 1 = lower mask
        "mvn x26, x26\n\t"            // x26 = ~lower mask = upper bits set
        "orr x17, x17, x26\n\t"       // Set upper bits
        "9:\n\t"
        // Store result
        "cmp x20, #31\n\t"
        "b.eq 10f\n\t"
        "add x0, x29, #16\n\t"
        "add x0, x0, x20, lsl #3\n\t"
        "str x17, [x0]\n\t"
        "b 11f\n\t"
        "10:\n\t"
        "str x17, [x29, #264]\n\t"   // xzr
        "11:\n\t"
        // Restore guest registers
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

tcti_gadget_t gadget_sbfm = gadget_sbfm_impl;

__attribute__((naked)) void gadget_bfm_impl(void) {
    asm volatile(
        "ldr x19, [x28], #8\n\t"  // fault_pc
        "ldr x20, [x28], #8\n\t"  // rd
        "ldr x21, [x28], #8\n\t"  // rn
        "ldr x22, [x28], #8\n\t"  // immr
        "ldr x23, [x28], #8\n\t"  // imms
        "ldr x24, [x28], #8\n\t"  // is_64bit
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        "cmp x21, #31\n\t"
        "b.eq 1f\n\t"
        "add x0, x29, #16\n\t"
        "add x0, x0, x21, lsl #3\n\t"
        "ldr x0, [x0]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "ldr x0, [x29, #264]\n\t"
        "2:\n\t"
        "cmp x20, #31\n\t"
        "b.eq 12f\n\t"
        "add x1, x29, #16\n\t"
        "add x1, x1, x20, lsl #3\n\t"
        "ldr x1, [x1]\n\t"
        "b 13f\n\t"
        "12:\n\t"
        "ldr x1, [x29, #264]\n\t"
        "13:\n\t"
        "mov x2, #0xffffffff\n\t"
        "mov x3, #-1\n\t"
        "cmp x24, #0\n\t"
        "csel x3, x2, x3, eq\n\t"
        "cmp x24, #0\n\t"
        "mov x2, #32\n\t"
        "mov x4, #64\n\t"
        "csel x2, x2, x4, eq\n\t"
        "and x0, x0, x3\n\t"
        "and x1, x1, x3\n\t"
        "sub x4, x2, x22\n\t"
        "sub x5, x2, #1\n\t"
        "and x4, x4, x5\n\t"
        "lsr x6, x0, x22\n\t"
        "lsl x7, x0, x4\n\t"
        "orr x6, x6, x7\n\t"
        "and x6, x6, x3\n\t"
        "cmp x23, x22\n\t"
        "b.hs 3f\n\t"
        "add x9, x23, #1\n\t"
        "add x9, x9, x2\n\t"
        "sub x9, x9, x22\n\t"
        "b 4f\n\t"
        "3:\n\t"
        "sub x9, x23, x22\n\t"
        "add x9, x9, #1\n\t"
        "4:\n\t"
        "cmp x9, x2\n\t"
        "b.eq 5f\n\t"
        "mov x10, #1\n\t"
        "lsl x10, x10, x9\n\t"
        "sub x10, x10, #1\n\t"
        "b 6f\n\t"
        "5:\n\t"
        "mov x10, #-1\n\t"
        "6:\n\t"
        "and x10, x10, x3\n\t"
        "and x11, x6, x10\n\t"
        "mvn x12, x10\n\t"
        "and x1, x1, x12\n\t"
        "orr x11, x11, x1\n\t"
        "and x11, x11, x3\n\t"
        "cmp x20, #31\n\t"
        "b.eq 10f\n\t"
        "add x12, x29, #16\n\t"
        "add x12, x12, x20, lsl #3\n\t"
        "str x11, [x12]\n\t"
        "b 11f\n\t"
        "10:\n\t"
        "str x11, [x29, #264]\n\t"
        "11:\n\t"
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

tcti_gadget_t gadget_bfm = gadget_bfm_impl;

__attribute__((naked)) void gadget_ubfm_impl(void) {
    asm volatile(
        "ldr x19, [x28], #8\n\t"  // fault_pc
        "ldr x20, [x28], #8\n\t"  // rd
        "ldr x21, [x28], #8\n\t"  // rn
        "ldr x22, [x28], #8\n\t"  // immr
        "ldr x23, [x28], #8\n\t"  // imms
        "ldr x24, [x28], #8\n\t"  // is_64bit
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        "cmp x21, #31\n\t"
        "b.eq 1f\n\t"
        "add x0, x29, #16\n\t"
        "add x0, x0, x21, lsl #3\n\t"
        "ldr x0, [x0]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "ldr x0, [x29, #264]\n\t"
        "2:\n\t"
        "mov x2, #0xffffffff\n\t"
        "mov x3, #-1\n\t"
        "cmp x24, #0\n\t"
        "csel x3, x2, x3, eq\n\t"
        "cmp x24, #0\n\t"
        "mov x2, #32\n\t"
        "mov x4, #64\n\t"
        "csel x2, x2, x4, eq\n\t"
        "and x0, x0, x3\n\t"
        "sub x4, x2, x22\n\t"
        "sub x5, x2, #1\n\t"
        "and x4, x4, x5\n\t"
        "lsr x6, x0, x22\n\t"
        "lsl x7, x0, x4\n\t"
        "orr x6, x6, x7\n\t"
        "and x6, x6, x3\n\t"
        "cmp x23, x22\n\t"
        "b.hs 3f\n\t"
        "add x9, x23, #1\n\t"
        "add x9, x9, x2\n\t"
        "sub x9, x9, x22\n\t"
        "b 4f\n\t"
        "3:\n\t"
        "sub x9, x23, x22\n\t"
        "add x9, x9, #1\n\t"
        "4:\n\t"
        "cmp x9, x2\n\t"
        "b.eq 5f\n\t"
        "mov x10, #1\n\t"
        "lsl x10, x10, x9\n\t"
        "sub x10, x10, #1\n\t"
        "b 6f\n\t"
        "5:\n\t"
        "mov x10, #-1\n\t"
        "6:\n\t"
        "and x10, x10, x3\n\t"
        "and x11, x6, x10\n\t"
        "cmp x20, #31\n\t"
        "b.eq 10f\n\t"
        "add x12, x29, #16\n\t"
        "add x12, x12, x20, lsl #3\n\t"
        "str x11, [x12]\n\t"
        "b 11f\n\t"
        "10:\n\t"
        "str x11, [x29, #264]\n\t"
        "11:\n\t"
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

tcti_gadget_t gadget_ubfm = gadget_ubfm_impl;

__attribute__((naked)) void gadget_ldr_x_impl(void) {
    asm volatile(
        // Load parameters from bytecode
        "ldr x19, [x28], #8\n\t"     // fault_pc
        "ldr x20, [x28], #8\n\t"     // Rt (destination reg)
        "ldr x21, [x28], #8\n\t"     // Rn (base reg)
        "ldr x22, [x28], #8\n\t"     // immediate offset
        "ldr x23, [x28], #8\n\t"     // size
        "ldr x24, [x28], #8\n\t"     // idx_mode
        "ldr x25, [x28], #8\n\t"     // meta
        
        // Patch 1B.1: Hot-hot fast path
        // Requirements: both Rn and Rt in 0-15, 64-bit, offset mode, meta=0
        "cmp x20, #16\n\t"           // Is Rt hot (0-15)?
        "b.hs 91f\n\t"               // Branch to nonhot counter
        "cmp x21, #16\n\t"           // Is Rn hot (0-15)?
        "b.hs 91f\n\t"
        "cmp x23, #3\n\t"            // Is size 64-bit?
        "b.ne 92f\n\t"               // Branch to size counter
        "cmp x24, #0\n\t"            // Is idx_mode offset (no writeback)?
        "b.ne 93f\n\t"               // Branch to idxmode counter
        "cmp x25, #0\n\t"            // Is meta 0 (no reg offset, not signed)?
        "b.ne 94f\n\t"               // Branch to meta counter
        
        // Get base register value (hot, in x1-x16) using computed goto
        // Branch table for Rn 0-15
        "adr x26, 70f\n\t"           // x26 = base of branch table
        "add x26, x26, x21, lsl #2\n\t" // x26 = &table[Rn] (b instructions are 4 bytes)
        "br x26\n\t"
        
        // Branch table - each entry is a direct branch to the load code
        "70:\n\t"
        "b 80f\n\t"                   // Rn=0 -> load from x1
        "b 81f\n\t"                   // Rn=1 -> load from x2
        "b 82f\n\t"                   // Rn=2 -> load from x3
        "b 83f\n\t"                   // Rn=3 -> load from x4
        "b 84f\n\t"                   // Rn=4 -> load from x5
        "b 85f\n\t"                   // Rn=5 -> load from x6
        "b 86f\n\t"                   // Rn=6 -> load from x7
        "b 87f\n\t"                   // Rn=7 -> load from x8
        "b 88f\n\t"                   // Rn=8 -> load from x9
        "b 89f\n\t"                   // Rn=9 -> load from x10
        "b 100f\n\t"                  // Rn=10 -> load from x11
        "b 101f\n\t"                  // Rn=11 -> load from x12
        "b 102f\n\t"                  // Rn=12 -> load from x13
        "b 103f\n\t"                  // Rn=13 -> load from x14
        "b 104f\n\t"                  // Rn=14 -> load from x15
        "b 105f\n\t"                  // Rn=15 -> load from x16
        
        // Load base register value into x17
        "80:\n\tmov x17, x1\n\tb 110f\n\t"
        "81:\n\tmov x17, x2\n\tb 110f\n\t"
        "82:\n\tmov x17, x3\n\tb 110f\n\t"
        "83:\n\tmov x17, x4\n\tb 110f\n\t"
        "84:\n\tmov x17, x5\n\tb 110f\n\t"
        "85:\n\tmov x17, x6\n\tb 110f\n\t"
        "86:\n\tmov x17, x7\n\tb 110f\n\t"
        "87:\n\tmov x17, x8\n\tb 110f\n\t"
        "88:\n\tmov x17, x9\n\tb 110f\n\t"
        "89:\n\tmov x17, x10\n\tb 110f\n\t"
        "100:\n\tmov x17, x11\n\tb 110f\n\t"
        "101:\n\tmov x17, x12\n\tb 110f\n\t"
        "102:\n\tmov x17, x13\n\tb 110f\n\t"
        "103:\n\tmov x17, x14\n\tb 110f\n\t"
        "104:\n\tmov x17, x15\n\tb 110f\n\t"
        "105:\n\tmov x17, x16\n\tb 110f\n\t"
        
        // Continue after base register load
        "110:\n\t"
        
        // Add immediate offset: x17 = base + offset
        "add x17, x17, x22\n\t"
        
        // Check alignment: addr & 7 == 0
        "tst x17, #7\n\t"
        "b.ne 95f\n\t"               // Branch to align counter
        
        // Check cross-page: (addr & 0xFFF) <= 0xFF8
        "and x13, x17, #0xFFF\n\t"
        "cmp x13, #0xFF8\n\t"
        "b.hi 96f\n\t"               // Branch to crosspg counter
        
        // Inline TLB lookup
        // All original args (x19-x25) remain stable
        // Use x26, x27 as scratch for TLB operations
        
        "ldr x26, [x29, #344]\n\t"   // x26 = cpu->tlb
        "cbz x26, 98f\n\t"           // Branch to notlb counter
        
        // TLB index: ((addr >> 12) & 1023) ^ (addr >> 22)
        "lsr x27, x17, #12\n\t"
        "and x27, x27, #1023\n\t"
        "lsr x13, x17, #22\n\t"
        "eor x27, x27, x13\n\t"
        
        // Load tlb entry at &entries[index]
        // entries is at offset 32 in struct tlb (64-bit page fields)
        "add x13, x26, #32\n\t"      // x13 = &tlb->entries[0]
        "mov x14, #24\n\t"           // x14 = sizeof(tlb_entry)
        "madd x13, x27, x14, x13\n\t" // x13 = &tlb->entries[index] (x13 + x27*24)
        "ldr x27, [x13]\n\t"          // x27 = entry.page
        
        // Compare page (clear lower 12 bits via shift)
        "lsr x26, x17, #12\n\t"      // x26 = addr >> 12
        "lsl x26, x26, #12\n\t"      // x26 = (addr >> 12) << 12 = page base
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t"               // Branch to tlbmiss counter
        
        // Compute host address and load
        // data_minus_addr is at offset 16 in tlb_entry (after two 8-byte page fields)
        "ldr x27, [x13, #16]\n\t"     // x27 = entry.data_minus_addr
        "add x17, x27, x17\n\t"       // x17 = host address
        "ldr x13, [x17]\n\t"          // x13 = loaded value
        
        // Store to hot destination register using computed goto
        // x20 still holds original Rt (0-15)
        "adr x26, 120f\n\t"           // x26 = base of store table
        "add x26, x26, x20, lsl #2\n\t" // x26 = &table[Rt] (b instructions are 4 bytes)
        "br x26\n\t"
        
        // Store branch table
        "120:\n\t"
        "b 130f\n\t" "b 131f\n\t" "b 132f\n\t" "b 133f\n\t"
        "b 134f\n\t" "b 135f\n\t" "b 136f\n\t" "b 137f\n\t"
        "b 138f\n\t" "b 139f\n\t" "b 140f\n\t" "b 141f\n\t"
        "b 142f\n\t" "b 143f\n\t" "b 144f\n\t" "b 145f\n\t"
        
        // Store to destination register
        "130:\n\tmov x1, x13\n\tb 150f\n\t"
        "131:\n\tmov x2, x13\n\tb 150f\n\t"
        "132:\n\tmov x3, x13\n\tb 150f\n\t"
        "133:\n\tmov x4, x13\n\tb 150f\n\t"
        "134:\n\tmov x5, x13\n\tb 150f\n\t"
        "135:\n\tmov x6, x13\n\tb 150f\n\t"
        "136:\n\tmov x7, x13\n\tb 150f\n\t"
        "137:\n\tmov x8, x13\n\tb 150f\n\t"
        "138:\n\tmov x9, x13\n\tb 150f\n\t"
        "139:\n\tmov x10, x13\n\tb 150f\n\t"
        "140:\n\tmov x11, x13\n\tb 150f\n\t"
        "141:\n\tmov x12, x13\n\tb 150f\n\t"
        "142:\n\tmov x13, x13\n\tb 150f\n\t"
        "143:\n\tmov x14, x13\n\tb 150f\n\t"
        "144:\n\tmov x15, x13\n\tb 150f\n\t"
        "145:\n\tmov x16, x13\n\tb 150f\n\t"
        
        // Fast path complete - increment counter and advance to next gadget
        "150:\n\t"
        "ldr x26, [x29, %[ldr_fast_hits_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fast_hits_off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        
        // Per-reason fallback counters
        "91:\n\t"  // TCTI_FALLBACK_NONHOT
        "ldr x26, [x29, %[ldr_fallback_nonhot_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_nonhot_off]]\n\t"
        "b 99f\n\t"
        
        "92:\n\t"  // TCTI_FALLBACK_SIZE
        "ldr x26, [x29, %[ldr_fallback_size_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_size_off]]\n\t"
        "b 99f\n\t"
        
        "93:\n\t"  // TCTI_FALLBACK_IDXMODE
        "ldr x26, [x29, %[ldr_fallback_idxmode_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_idxmode_off]]\n\t"
        "b 99f\n\t"
        
        "94:\n\t"  // TCTI_FALLBACK_META
        "ldr x26, [x29, %[ldr_fallback_meta_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_meta_off]]\n\t"
        "b 99f\n\t"
        
        "95:\n\t"  // TCTI_FALLBACK_ALIGN
        "ldr x26, [x29, %[ldr_fallback_align_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_align_off]]\n\t"
        "b 99f\n\t"
        
        "96:\n\t"  // TCTI_FALLBACK_CROSSPG
        "ldr x26, [x29, %[ldr_fallback_crosspg_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_crosspg_off]]\n\t"
        "b 99f\n\t"
        
        "97:\n\t"  // TCTI_FALLBACK_TLBMISS
        "ldr x26, [x29, %[ldr_fallback_tlbmiss_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_tlbmiss_off]]\n\t"
        "b 99f\n\t"
        
        "98:\n\t"  // TCTI_FALLBACK_NOTLB
        "ldr x26, [x29, %[ldr_fallback_notlb_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_notlb_off]]\n\t"
        // fall through to 99
        
        // Common slow path after per-reason counter (label 99)
        "99:\n\t"
        "ldr x26, [x29, %[ldr_fallback_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_off]]\n\t"
        // Save registers and call C helper
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        "bl _tcti_c_call_prologue\n\t"
        "mov x0, x29\n\t"
        "mov x1, x19\n\t"            // fault_pc
        "mov x2, x20\n\t"            // Rt
        "mov x3, x21\n\t"            // Rn
        "mov x4, x22\n\t"            // imm
        "mov x5, x23\n\t"            // size
        "mov x6, x24\n\t"            // idx_mode
        "mov x7, x25\n\t"            // meta
        "bl _a64_tcti_ldr_x_helper\n\t"
        "bl _tcti_c_call_epilogue\n\t"
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "cmp x0, #0\n\t"
        "b.ne 1f\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        "1:\n\t"
        "b _tcti_exit_block\n\t"
        :
        : [ldr_fast_hits_off] "i" (STAT_LDR_FAST_HITS_OFFSET),
          [ldr_fallback_off] "i" (STAT_LDR_FALLBACK_OFFSET),
          [ldr_fallback_nonhot_off] "i" (STAT_LDR_FALLBACK_NONHOT_OFFSET),
          [ldr_fallback_size_off] "i" (STAT_LDR_FALLBACK_SIZE_OFFSET),
          [ldr_fallback_idxmode_off] "i" (STAT_LDR_FALLBACK_IDXMODE_OFFSET),
          [ldr_fallback_meta_off] "i" (STAT_LDR_FALLBACK_META_OFFSET),
          [ldr_fallback_align_off] "i" (STAT_LDR_FALLBACK_ALIGN_OFFSET),
          [ldr_fallback_crosspg_off] "i" (STAT_LDR_FALLBACK_CROSSPG_OFFSET),
          [ldr_fallback_tlbmiss_off] "i" (STAT_LDR_FALLBACK_TLBMISS_OFFSET),
          [ldr_fallback_notlb_off] "i" (STAT_LDR_FALLBACK_NOTLB_OFFSET)
        : "x13", "x26", "x27", "memory"
    );
}

tcti_gadget_t gadget_ldr_x = gadget_ldr_x_impl;

__attribute__((naked)) void gadget_str_x_impl(void) {
    asm volatile(
        // Load parameters from bytecode
        "ldr x19, [x28], #8\n\t"     // fault_pc
        "ldr x20, [x28], #8\n\t"     // Rt (source reg)
        "ldr x21, [x28], #8\n\t"     // Rn (base reg)
        "ldr x22, [x28], #8\n\t"     // immediate offset
        "ldr x23, [x28], #8\n\t"     // size
        "ldr x24, [x28], #8\n\t"     // idx_mode
        "ldr x25, [x28], #8\n\t"     // meta

        // Patch 1B.2: Hot-hot fast path for STR
        // Requirements: both Rn and Rt in 0-15, 64-bit, offset mode, meta=0
        "cmp x20, #16\n\t"           // Is Rt hot (0-15)?
        "b.hs 91f\n\t"               // Branch to nonhot counter
        "cmp x21, #16\n\t"           // Is Rn hot (0-15)?
        "b.hs 91f\n\t"
        "cmp x23, #3\n\t"            // Is size 64-bit?
        "b.ne 92f\n\t"               // Branch to size counter
        "cmp x24, #0\n\t"            // Is idx_mode offset (no writeback)?
        "b.ne 93f\n\t"               // Branch to idxmode counter
        "cmp x25, #0\n\t"            // Is meta 0 (no reg offset, not signed)?
        "b.ne 94f\n\t"               // Branch to meta counter

        // Get base register value (hot, in x1-x16) using computed goto
        // Branch table for Rn 0-15
        "adr x26, 70f\n\t"           // x26 = base of branch table
        "add x26, x26, x21, lsl #2\n\t" // x26 = &table[Rn] (b instructions are 4 bytes)
        "br x26\n\t"

        // Branch table - each entry is a direct branch to the load code
        "70:\n\t"
        "b 80f\n\t"                   // Rn=0 -> load from x1
        "b 81f\n\t"                   // Rn=1 -> load from x2
        "b 82f\n\t"                   // Rn=2 -> load from x3
        "b 83f\n\t"                   // Rn=3 -> load from x4
        "b 84f\n\t"                   // Rn=4 -> load from x5
        "b 85f\n\t"                   // Rn=5 -> load from x6
        "b 86f\n\t"                   // Rn=6 -> load from x7
        "b 87f\n\t"                   // Rn=7 -> load from x8
        "b 88f\n\t"                   // Rn=8 -> load from x9
        "b 89f\n\t"                   // Rn=9 -> load from x10
        "b 100f\n\t"                  // Rn=10 -> load from x11
        "b 101f\n\t"                  // Rn=11 -> load from x12
        "b 102f\n\t"                  // Rn=12 -> load from x13
        "b 103f\n\t"                  // Rn=13 -> load from x14
        "b 104f\n\t"                  // Rn=14 -> load from x15
        "b 105f\n\t"                  // Rn=15 -> load from x16

        // Load base register value into x17
        "80:\n\tmov x17, x1\n\tb 110f\n\t"
        "81:\n\tmov x17, x2\n\tb 110f\n\t"
        "82:\n\tmov x17, x3\n\tb 110f\n\t"
        "83:\n\tmov x17, x4\n\tb 110f\n\t"
        "84:\n\tmov x17, x5\n\tb 110f\n\t"
        "85:\n\tmov x17, x6\n\tb 110f\n\t"
        "86:\n\tmov x17, x7\n\tb 110f\n\t"
        "87:\n\tmov x17, x8\n\tb 110f\n\t"
        "88:\n\tmov x17, x9\n\tb 110f\n\t"
        "89:\n\tmov x17, x10\n\tb 110f\n\t"
        "100:\n\tmov x17, x11\n\tb 110f\n\t"
        "101:\n\tmov x17, x12\n\tb 110f\n\t"
        "102:\n\tmov x17, x13\n\tb 110f\n\t"
        "103:\n\tmov x17, x14\n\tb 110f\n\t"
        "104:\n\tmov x17, x15\n\tb 110f\n\t"
        "105:\n\tmov x17, x16\n\tb 110f\n\t"

        // Continue after base register load
        "110:\n\t"

        // Add immediate offset: x17 = base + offset
        "add x17, x17, x22\n\t"

        // Check alignment: addr & 7 == 0
        "tst x17, #7\n\t"
        "b.ne 95f\n\t"               // Branch to align counter

        // Check cross-page: (addr & 0xFFF) <= 0xFF8
        "and x13, x17, #0xFFF\n\t"
        "cmp x13, #0xFF8\n\t"
        "b.hi 96f\n\t"               // Branch to crosspg counter

        // Get source register value (Rt, hot, in x1-x16) using computed goto
        // x20 still holds original Rt (0-15)
        "adr x26, 120f\n\t"           // x26 = base of branch table
        "add x26, x26, x20, lsl #2\n\t" // x26 = &table[Rt] (b instructions are 4 bytes)
        "br x26\n\t"

        // Branch table for source value load
        "120:\n\t"
        "b 130f\n\t"                   // Rt=0 -> load from x1
        "b 131f\n\t"                   // Rt=1 -> load from x2
        "b 132f\n\t"                   // Rt=2 -> load from x3
        "b 133f\n\t"                   // Rt=3 -> load from x4
        "b 134f\n\t"                   // Rt=4 -> load from x5
        "b 135f\n\t"                   // Rt=5 -> load from x6
        "b 136f\n\t"                   // Rt=6 -> load from x7
        "b 137f\n\t"                   // Rt=7 -> load from x8
        "b 138f\n\t"                   // Rt=8 -> load from x9
        "b 139f\n\t"                   // Rt=9 -> load from x10
        "b 140f\n\t"                   // Rt=10 -> load from x11
        "b 141f\n\t"                   // Rt=11 -> load from x12
        "b 142f\n\t"                   // Rt=12 -> load from x13
        "b 143f\n\t"                   // Rt=13 -> load from x14
        "b 144f\n\t"                   // Rt=14 -> load from x15
        "b 145f\n\t"                   // Rt=15 -> load from x16

        // Load source register value into x13
        "130:\n\tmov x13, x1\n\tb 150f\n\t"
        "131:\n\tmov x13, x2\n\tb 150f\n\t"
        "132:\n\tmov x13, x3\n\tb 150f\n\t"
        "133:\n\tmov x13, x4\n\tb 150f\n\t"
        "134:\n\tmov x13, x5\n\tb 150f\n\t"
        "135:\n\tmov x13, x6\n\tb 150f\n\t"
        "136:\n\tmov x13, x7\n\tb 150f\n\t"
        "137:\n\tmov x13, x8\n\tb 150f\n\t"
        "138:\n\tmov x13, x9\n\tb 150f\n\t"
        "139:\n\tmov x13, x10\n\tb 150f\n\t"
        "140:\n\tmov x13, x11\n\tb 150f\n\t"
        "141:\n\tmov x13, x12\n\tb 150f\n\t"
        "142:\n\tmov x13, x13\n\tb 150f\n\t"
        "143:\n\tmov x13, x14\n\tb 150f\n\t"
        "144:\n\tmov x13, x15\n\tb 150f\n\t"
        "145:\n\tmov x13, x16\n\tb 150f\n\t"

        // Continue after source register load
        "150:\n\t"

        // Inline TLB lookup
        // CRITICAL: x20 contains original Rt - do NOT clobber it
        // Use x14 (scratch) instead of x20 for TLB operations

        "ldr x26, [x29, #344]\n\t"   // x26 = cpu->tlb
        "cbz x26, 98f\n\t"           // Branch to notlb counter

        // TLB index: ((addr >> 12) & 1023) ^ (addr >> 22)
        "lsr x27, x17, #12\n\t"
        "and x27, x27, #1023\n\t"
        "lsr x14, x17, #22\n\t"       // Use x14 instead of x20
        "eor x27, x27, x14\n\t"

        // Load tlb entry at &entries[index]
        // entries is at offset 32 in struct tlb (64-bit page fields)
        "add x14, x26, #32\n\t"       // x14 = &tlb->entries[0]
        "mov x15, #24\n\t"            // x15 = sizeof(tlb_entry)
        "madd x14, x27, x15, x14\n\t" // x14 = &tlb->entries[index] (x14 + x27*24)
        "ldr x27, [x14]\n\t"          // x27 = entry.page

        // Compare page (clear lower 12 bits via shift)
        "lsr x26, x17, #12\n\t"      // x26 = addr >> 12
        "lsl x26, x26, #12\n\t"      // x26 = (addr >> 12) << 12 = page base
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t"               // Branch to tlbmiss counter

        // Compute host address and store
        // data_minus_addr is at offset 16 in tlb_entry
        "ldr x27, [x14, #16]\n\t"     // x27 = entry.data_minus_addr
        "add x17, x27, x17\n\t"       // x17 = host address
        "str x13, [x17]\n\t"          // store value from x13

        // Fast path complete - increment counter and advance to next gadget
        "ldr x26, [x29, %[str_fast_hits_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fast_hits_off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        
        // Per-reason fallback counters for STR
        "91:\n\t"  // TCTI_FALLBACK_NONHOT
        "ldr x26, [x29, %[str_fallback_nonhot_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_nonhot_off]]\n\t"
        "b 99f\n\t"
        
        "92:\n\t"  // TCTI_FALLBACK_SIZE
        "ldr x26, [x29, %[str_fallback_size_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_size_off]]\n\t"
        "b 99f\n\t"
        
        "93:\n\t"  // TCTI_FALLBACK_IDXMODE
        "ldr x26, [x29, %[str_fallback_idxmode_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_idxmode_off]]\n\t"
        "b 99f\n\t"
        
        "94:\n\t"  // TCTI_FALLBACK_META
        "ldr x26, [x29, %[str_fallback_meta_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_meta_off]]\n\t"
        "b 99f\n\t"
        
        "95:\n\t"  // TCTI_FALLBACK_ALIGN
        "ldr x26, [x29, %[str_fallback_align_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_align_off]]\n\t"
        "b 99f\n\t"
        
        "96:\n\t"  // TCTI_FALLBACK_CROSSPG
        "ldr x26, [x29, %[str_fallback_crosspg_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_crosspg_off]]\n\t"
        "b 99f\n\t"
        
        "97:\n\t"  // TCTI_FALLBACK_TLBMISS
        "ldr x26, [x29, %[str_fallback_tlbmiss_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_tlbmiss_off]]\n\t"
        "b 99f\n\t"
        
        "98:\n\t"  // TCTI_FALLBACK_NOTLB
        "ldr x26, [x29, %[str_fallback_notlb_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_notlb_off]]\n\t"
        // fall through to 99
        
        // Common slow path after per-reason counter (label 99)
        "99:\n\t"
        "ldr x26, [x29, %[str_fallback_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_off]]\n\t"
        // Save registers and call C helper
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        "bl _tcti_c_call_prologue\n\t"
        "mov x0, x29\n\t"
        "mov x1, x19\n\t"            // fault_pc
        "mov x2, x20\n\t"            // Rt
        "mov x3, x21\n\t"            // Rn
        "mov x4, x22\n\t"            // imm
        "mov x5, x23\n\t"            // size
        "mov x6, x24\n\t"            // idx_mode
        "mov x7, x25\n\t"            // meta
        "bl _a64_tcti_str_x_helper\n\t"
        "bl _tcti_c_call_epilogue\n\t"
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "cmp x0, #0\n\t"
        "b.ne 1f\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        "1:\n\t"
        "b _tcti_exit_block\n\t"
        :
        : [str_fast_hits_off] "i" (STAT_STR_FAST_HITS_OFFSET),
          [str_fallback_off] "i" (STAT_STR_FALLBACK_OFFSET),
          [str_fallback_nonhot_off] "i" (STAT_STR_FALLBACK_NONHOT_OFFSET),
          [str_fallback_size_off] "i" (STAT_STR_FALLBACK_SIZE_OFFSET),
          [str_fallback_idxmode_off] "i" (STAT_STR_FALLBACK_IDXMODE_OFFSET),
          [str_fallback_meta_off] "i" (STAT_STR_FALLBACK_META_OFFSET),
          [str_fallback_align_off] "i" (STAT_STR_FALLBACK_ALIGN_OFFSET),
          [str_fallback_crosspg_off] "i" (STAT_STR_FALLBACK_CROSSPG_OFFSET),
          [str_fallback_tlbmiss_off] "i" (STAT_STR_FALLBACK_TLBMISS_OFFSET),
          [str_fallback_notlb_off] "i" (STAT_STR_FALLBACK_NOTLB_OFFSET)
        : "x13", "x26", "x27", "memory"
    );
}

tcti_gadget_t gadget_str_x = gadget_str_x_impl;

__attribute__((naked)) void gadget_nop_impl(void) {
    asm volatile(
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

tcti_gadget_t gadget_nop = gadget_nop_impl;

__attribute__((naked)) void gadget_svc_impl(void) {
    asm volatile(
        "ldr x0, [x28], #8\n\t"
        "str x0, [x29, #272]\n\t"
        "mov x0, #1\n\t"
        "b _tcti_exit_block\n\t"
    );
}

tcti_gadget_t gadget_svc = gadget_svc_impl;
