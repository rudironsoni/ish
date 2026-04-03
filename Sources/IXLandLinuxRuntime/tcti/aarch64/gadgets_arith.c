/*
 * Arithmetic gadget implementations
 * These provide the actual computation for arithmetic operations
 * that need more than just a simple instruction
 */

#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

// Flag-setting variants of arithmetic operations
// These update NZCV flags in addition to computing the result

// Legacy test-only gadgets kept out of the generated symbol namespace.
__attribute__((naked)) void gadget_adds_reg_legacy_0_1_2(void) {
    asm volatile(
        "adds x1, x2, x3\n\t"      // Set flags
        "ldr x27, [x28], #8\n\t"  // Load next gadget
        "br x27\n\t"
    );
}

// SUBS (flag-setting subtract)
__attribute__((naked)) void gadget_subs_reg_legacy_0_1_2(void) {
    asm volatile(
        "subs x1, x2, x3\n\t"      // Set flags
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// CMP (compare - subtract that only sets flags)
__attribute__((naked)) void gadget_cmp_legacy_0_1(void) {
    asm volatile(
        "subs xzr, x1, x2\n\t"     // Compare, discard result
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// CMN (compare negative - add that only sets flags)
__attribute__((naked)) void gadget_cmn_legacy_0_1(void) {
    asm volatile(
        "adds xzr, x1, x2\n\t"     // Compare negative, discard result
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// TST (test - AND that only sets flags)
__attribute__((naked)) void gadget_tst_legacy_0_1(void) {
    asm volatile(
        "ands xzr, x1, x2\n\t"     // Test, discard result
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// Load/store for TCTI-mapped registers
// These handle reading from/writing to the CPU state for memory-backed regs

// Load memory-backed register into x0 (for operations)
__attribute__((naked)) void gadget_load_xreg(int reg) {
    // Load x[reg+16] from cpu state into x0
    // reg is in range 0-15 (maps to x16-x30) or -1 for SP
    asm volatile(
        "cmp x0, #-1\n\t"
        "b.eq 1f\n\t"               // SP case
        "add x1, x29, %[off]\n\t"   // Calculate offset
        "add x1, x1, x0, lsl #3\n\t"
        "ldr x0, [x1]\n\t"          // Load value
        "b 2f\n\t"
        "1:\n\t"
        "ldr x0, [x29, %[sp_off]]\n\t" // Load SP
        "2:\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (16*8), [sp_off] "i" (offsetof(struct cpu_state, sp))
    );
}

// Store x0 to memory-backed register
__attribute__((naked)) void gadget_store_xreg(int reg) {
    asm volatile(
        "cmp x0, #-1\n\t"
        "b.eq 1f\n\t"
        "add x1, x29, %[off]\n\t"
        "add x1, x1, x0, lsl #3\n\t"
        "str %[val], [x1]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "str %[val], [x29, %[sp_off]]\n\t"
        "2:\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        :
        : [off] "i" (16*8), [sp_off] "i" (offsetof(struct cpu_state, sp)), [val] "r" ((uint64_t)0)
    );
}
