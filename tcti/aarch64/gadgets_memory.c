/*
 * Memory access gadgets for TCTI
 *
 * Memory-backed registers (x15-x30, SP) are stored in cpu_state memory.
 * To operate on them, we:
 *   1. Load into temp register (x16 for values, x17 for addresses)
 *   2. Execute operation
 *   3. Store back to memory
 *
 * Register mapping:
 *   x0-x14 (guest)  -> x1-x15 (host)   [TCTI-mapped, always hot]
 *   x15-x30 (guest) -> memory only     [load/store via gadgets]
 *   SP (guest)      -> memory only     [load/store via gadgets]
 *   x16-x18 (host)  -> temps           [for memory-backed register ops]
 *   x27-x29 (host)  -> TCTI internals  [gadget ptr, bytecode, cpu_state]
 */

#include "tcti/aarch64/gadgets_tcti.h"
#include "emu/aarch64/cpu.h"
#include <stddef.h>

// Verify offset assumptions at compile time
#define XREG_OFFSET(n) (offsetof(struct cpu_state, x[n]))
#define SP_OFFSET offsetof(struct cpu_state, sp)

// Guest x[0] is at offset 16 (after mmu pointer and cycle counter)
static_assert(XREG_OFFSET(0) == 16, "x[0] offset check");
static_assert(SP_OFFSET == 264, "SP offset check");

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
//   - Loads/stores one guest register to/from temp x16
//   - Advances to next gadget via x28 bytecode pointer
//   - Branches to next gadget
//
// Index mapping: 0=x16, 1=x17, ..., 14=x30 (15 entries)
// ============================================================================

// Load guest x[16 + idx] into host temp x16
#define GEN_LOAD_XREG(idx) \
    __attribute__((naked)) void gadget_load_x##idx##_impl(void) { \
        asm volatile( \
            "ldr x16, [x29, %[off]]\n\t" \
            "ldr x27, [x28], #8\n\t" \
            "br x27\n\t" \
            : \
            : [off] "i" (XREG_OFFSET(16 + idx)) \
        ); \
    }

// Store host temp x16 to guest x[16 + idx]  
#define GEN_STORE_XREG(idx) \
    __attribute__((naked)) void gadget_store_x##idx##_impl(void) { \
        asm volatile( \
            "str x16, [x29, %[off]]\n\t" \
            "ldr x27, [x28], #8\n\t" \
            "br x27\n\t" \
            : \
            : [off] "i" (XREG_OFFSET(16 + idx)) \
        ); \
    }

// Generate load gadgets for x16-x30 (indices 0-14)
GEN_LOAD_XREG(0)   // x16
GEN_LOAD_XREG(1)   // x17
GEN_LOAD_XREG(2)   // x18
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
GEN_STORE_XREG(2)   // x18
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

// x30 (LR) load/store - uses x17 as temp since x16 may hold result
__attribute__((naked)) void gadget_load_x30_impl(void) {
    asm volatile(
        "ldr x17, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (XREG_OFFSET(30))
    );
}

__attribute__((naked)) void gadget_store_x30_impl(void) {
    asm volatile(
        "str x17, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (XREG_OFFSET(30))
    );
}

// SP load/store - uses x18 as temp
// These are the actual implementations matching header declarations
__attribute__((naked)) void gadget_load_sp(void) {
    asm volatile(
        "ldr x18, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (SP_OFFSET)
    );
}

__attribute__((naked)) void gadget_store_sp(void) {
    asm volatile(
        "str x18, [x29, %[off]]\n\t"
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
    gadget_load_x2_impl,   // x18
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
    gadget_store_x2_impl,   // x18
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
