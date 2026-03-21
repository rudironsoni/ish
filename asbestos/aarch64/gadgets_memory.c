/*
 * Memory access gadgets for TCTI
 *
 * Load/Store operations need TLB translation since we can't directly
 * access guest addresses. These gadgets call C helpers for the translation.
 */

#include "asbestos/aarch64/gadgets_tcti.h"
#include "emu/aarch64/cpu.h"
#include "emu/tlb.h"

// Forward declarations - implementations are at end of file
// Note: These are NOT static so they can be called from naked gadget functions
int a64_guest_load8(uint64_t addr, uint64_t *val);
int a64_guest_load4(uint64_t addr, uint32_t *val);
int a64_guest_load2(uint64_t addr, uint16_t *val);
int a64_guest_load1(uint64_t addr, uint8_t *val);
int a64_guest_store8(uint64_t addr, uint64_t val);
int a64_guest_store4(uint64_t addr, uint32_t val);
int a64_guest_store2(uint64_t addr, uint16_t val);
int a64_guest_store1(uint64_t addr, uint8_t val);

// LDR (64-bit) - xRt = [xRn + offset]
// For TCTI-mapped registers (x0-x15)
__attribute__((naked)) void gadget_ldr_x_0_1(void) {
    // Load x0 from address in x1
    // x1 = host x2, x0 = host x1
    // Call C helper: a64_guest_load8(cpu->x[1], &val)
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"    // Save callee-saved regs
        "mov x19, x1\n\t"                  // Save x1 (result reg)
        "mov x0, x2\n\t"                   // Address = x1 (host x2)
        "add x1, x29, %[off]\n\t"          // &cpu->x[0] as output
        "bl a64_guest_load8\n\t"           // Call helper
        "cbnz x0, 1f\n\t"                  // Check for fault
        "ldr x1, [x29, %[off]]\n\t"        // Load result to x1
        "b 2f\n\t"
        "1:\n\t"                           // Fault path
        "mov x27, #3\n\t"                  // TCTI_EXIT_FAULT
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"      // Restore regs
        "ldr x27, [x28], #8\n\t"           // Load next gadget
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// STR (64-bit) - [xRn + offset] = xRt
__attribute__((naked)) void gadget_str_x_0_1(void) {
    // Store x0 to address in x1
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x0, x2\n\t"                   // Address
        "ldr x1, [x29, %[off]]\n\t"        // Value = cpu->x[0]
        "bl a64_guest_store8\n\t"
        "cbnz x0, 1f\n\t"                  // Check for fault
        "b 2f\n\t"
        "1:\n\t"
        "mov x27, #3\n\t"                  // TCTI_EXIT_FAULT
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// LDR (32-bit) - wRt = [xRn + offset]
__attribute__((naked)) void gadget_ldr_w_0_1(void) {
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x19, x1\n\t"
        "mov x0, x2\n\t"
        "add x1, x29, %[off]\n\t"
        "bl a64_guest_load4\n\t"
        "cbnz x0, 1f\n\t"
        "ldr x1, [x29, %[off]]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov x27, #3\n\t"
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// STR (32-bit) - [xRn + offset] = wRt
__attribute__((naked)) void gadget_str_w_0_1(void) {
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x0, x2\n\t"
        "ldr x1, [x29, %[off]]\n\t"
        "bl a64_guest_store4\n\t"
        "cbnz x0, 1f\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov x27, #3\n\t"
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// LDRB (8-bit) - wRt = [xRn + offset]
__attribute__((naked)) void gadget_ldrb_w_0_1(void) {
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x19, x1\n\t"
        "mov x0, x2\n\t"
        "add x1, x29, %[off]\n\t"
        "bl a64_guest_load1\n\t"
        "cbnz x0, 1f\n\t"
        "ldr x1, [x29, %[off]]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov x27, #3\n\t"
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// STRB (8-bit) - [xRn + offset] = wRt
__attribute__((naked)) void gadget_strb_w_0_1(void) {
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x0, x2\n\t"
        "ldr x1, [x29, %[off]]\n\t"
        "bl a64_guest_store1\n\t"
        "cbnz x0, 1f\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov x27, #3\n\t"
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// LDRH (16-bit) - wRt = [xRn + offset]
__attribute__((naked)) void gadget_ldrh_w_0_1(void) {
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x19, x1\n\t"
        "mov x0, x2\n\t"
        "add x1, x29, %[off]\n\t"
        "bl a64_guest_load2\n\t"
        "cbnz x0, 1f\n\t"
        "ldr x1, [x29, %[off]]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov x27, #3\n\t"
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// STRH (16-bit) - [xRn + offset] = wRt
__attribute__((naked)) void gadget_strh_w_0_1(void) {
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x0, x2\n\t"
        "ldr x1, [x29, %[off]]\n\t"
        "bl a64_guest_store2\n\t"
        "cbnz x0, 1f\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov x27, #3\n\t"
        "2:\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (offsetof(struct cpu_state, x[0]))
    );
}

// LDR with immediate offset - most common case
// xRt = [xRn + #imm]
// This is a simplified version - full implementation needs offset decoding

// Generic load helper that uses the CPU state pointer
static inline uint64_t __attribute__((unused)) a64_do_load(struct cpu_state *cpu, uint64_t addr, int size) {
    uint64_t val = 0;
    int ret;
    switch (size) {
        case 1: ret = a64_guest_load1(addr, (uint8_t*)&val); break;
        case 2: ret = a64_guest_load2(addr, (uint16_t*)&val); break;
        case 4: ret = a64_guest_load4(addr, (uint32_t*)&val); break;
        case 8: ret = a64_guest_load8(addr, &val); break;
        default: ret = -1;
    }
    if (ret != 0) {
        cpu->fault_addr = addr;
        // Signal fault
    }
    return val;
}

static inline void __attribute__((unused)) a64_do_store(struct cpu_state *cpu, uint64_t addr, uint64_t val, int size) {
    int ret;
    switch (size) {
        case 1: ret = a64_guest_store1(addr, val); break;
        case 2: ret = a64_guest_store2(addr, val); break;
        case 4: ret = a64_guest_store4(addr, val); break;
        case 8: ret = a64_guest_store8(addr, val); break;
        default: ret = -1;
    }
    if (ret != 0) {
        cpu->fault_addr = addr;
        cpu->fault_was_write = 1;
        // Signal fault
    }
}

// C implementations of memory helpers - called from naked gadgets
// These do the actual TLB translation
// Note: These require access to current task's TLB via current->cpu.mmu

#include "kernel/task.h"

// Helper to get TLB from current task
static inline struct tlb *__attribute__((unused)) get_tlb(void) {
    // TLB is accessed via current task's MMU
    // For now, return NULL - caller must handle TLB miss
    return NULL;
}

// Helper functions for memory access - NOT static so gadgets can reference them
// These are called from naked gadget functions via BL
int a64_guest_load8(uint64_t addr, uint64_t *val) {
    struct cpu_state *cpu = &current->cpu;
    (void)addr;  // TLB access needs proper implementation
    (void)cpu;
    // Placeholder implementation
    *val = 0;
    return -1;  // Always fail - real implementation needs proper TLB lookup
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
    // Placeholder - real implementation needs proper TLB
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
