/*
 * Memory access gadgets for TCTI
 *
 * Load/Store operations need TLB translation since we can't directly
 * access guest addresses. These gadgets call C helpers for the translation.
 */

#include "tcti/aarch64/gadgets_tcti.h"
#include "emu/aarch64/cpu.h"
#include "emu/tlb.h"
#include "kernel/task.h"

// Memory access helper functions - iOS uses underscore prefix
// These are called from naked gadgets via BL instruction
int a64_guest_load8(uint64_t addr, uint64_t *val);
int a64_guest_load4(uint64_t addr, uint32_t *val);
int a64_guest_load2(uint64_t addr, uint16_t *val);
int a64_guest_load1(uint64_t addr, uint8_t *val);
int a64_guest_store8(uint64_t addr, uint64_t val);
int a64_guest_store4(uint64_t addr, uint32_t val);
int a64_guest_store2(uint64_t addr, uint16_t val);
int a64_guest_store1(uint64_t addr, uint8_t val);

// ============================================================================
// C implementations of memory helpers - called from naked gadgets
// ============================================================================

int a64_guest_load8(uint64_t addr, uint64_t *val) {
    (void)addr;
    *val = 0;
    return -1;  // Placeholder - needs proper TLB implementation
}

int a64_guest_load4(uint64_t addr, uint32_t *val) {
    (void)addr;
    *val = 0;
    return -1;
}

int a64_guest_load2(uint64_t addr, uint16_t *val) {
    (void)addr;
    *val = 0;
    return -1;
}

int a64_guest_load1(uint64_t addr, uint8_t *val) {
    (void)addr;
    *val = 0;
    return -1;
}

int a64_guest_store8(uint64_t addr, uint64_t val) {
    (void)addr;
    (void)val;
    return -1;
}

int a64_guest_store4(uint64_t addr, uint32_t val) {
    (void)addr;
    (void)val;
    return -1;
}

int a64_guest_store2(uint64_t addr, uint16_t val) {
    (void)addr;
    (void)val;
    return -1;
}

int a64_guest_store1(uint64_t addr, uint8_t val) {
    (void)addr;
    (void)val;
    return -1;
}

// ============================================================================
// Exit Gadget - Marks end of gadget stream
// ============================================================================

__attribute__((naked)) void gadget_exit_impl(void) {
    asm volatile(
        "mov x0, #0\n\t"               // Exit reason = normal
        "b _tcti_exit_block\n\t"       // Jump to exit handler (iOS uses underscore)
    );
}

// Export as function pointer
tcti_gadget_t gadget_exit = gadget_exit_impl;

// ============================================================================
// Memory-backed register load/store gadgets
// ============================================================================

// Load memory-backed register x[16+n] into host temp register x17
#define GEN_LOAD_XREG(n) \
    __attribute__((naked)) void gadget_load_x##n(void) { \
        asm volatile( \
            "ldr x17, [x29, %[off]]\n\t" \
            "ldr x27, [x28], #8\n\t" \
            "br x27\n\t" \
            : \
            : [off] "i" (offsetof(struct cpu_state, x[n])) \
        ); \
    }

// Store host temp register x17 back to memory-backed register x[16+n]
#define GEN_STORE_XREG(n) \
    __attribute__((naked)) void gadget_store_x##n(void) { \
        asm volatile( \
            "str x17, [x29, %[off]]\n\t" \
            "ldr x27, [x28], #8\n\t" \
            "br x27\n\t" \
            : \
            : [off] "i" (offsetof(struct cpu_state, x[n])) \
        ); \
    }

// Generate all 15 load/store gadgets for x16-x30
GEN_LOAD_XREG(16)
GEN_LOAD_XREG(17)
GEN_LOAD_XREG(18)
GEN_LOAD_XREG(19)
GEN_LOAD_XREG(20)
GEN_LOAD_XREG(21)
GEN_LOAD_XREG(22)
GEN_LOAD_XREG(23)
GEN_LOAD_XREG(24)
GEN_LOAD_XREG(25)
GEN_LOAD_XREG(26)
GEN_LOAD_XREG(27)
GEN_LOAD_XREG(28)
GEN_LOAD_XREG(29)
GEN_LOAD_XREG(30)

GEN_STORE_XREG(16)
GEN_STORE_XREG(17)
GEN_STORE_XREG(18)
GEN_STORE_XREG(19)
GEN_STORE_XREG(20)
GEN_STORE_XREG(21)
GEN_STORE_XREG(22)
GEN_STORE_XREG(23)
GEN_STORE_XREG(24)
GEN_STORE_XREG(25)
GEN_STORE_XREG(26)
GEN_STORE_XREG(27)
GEN_STORE_XREG(28)
GEN_STORE_XREG(29)
GEN_STORE_XREG(30)

// SP load/store - uses x18 as temp since x17 is used by other load/store
__attribute__((naked)) void gadget_load_sp(void) {
    asm volatile(
        "ldr x18, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, sp))
    );
}

__attribute__((naked)) void gadget_store_sp(void) {
    asm volatile(
        "str x18, [x29, %[off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, sp))
    );
}

// Table of load gadgets for x16-x30
const tcti_gadget_t gadget_load_xreg_16_to_30[15] = {
    gadget_load_x16, gadget_load_x17, gadget_load_x18, gadget_load_x19,
    gadget_load_x20, gadget_load_x21, gadget_load_x22, gadget_load_x23,
    gadget_load_x24, gadget_load_x25, gadget_load_x26, gadget_load_x27,
    gadget_load_x28, gadget_load_x29, gadget_load_x30
};

// Table of store gadgets for x16-x30
const tcti_gadget_t gadget_store_xreg_16_to_30[15] = {
    gadget_store_x16, gadget_store_x17, gadget_store_x18, gadget_store_x19,
    gadget_store_x20, gadget_store_x21, gadget_store_x22, gadget_store_x23,
    gadget_store_x24, gadget_store_x25, gadget_store_x26, gadget_store_x27,
    gadget_store_x28, gadget_store_x29, gadget_store_x30
};
