/*
 * Entry/exit gadgets for TCTI
 */

#include "asbestos/aarch64/gadgets_tcti.h"
#include "emu/aarch64/cpu.h"

// Block entry - sets up execution environment
__attribute__((naked)) void tcti_entry_block(void) {
    // x29 = cpu_state pointer (set by caller)
    // x28 = bytecode stream pointer (set by caller)
    asm volatile(
        // Load TCTI-mapped registers from CPU state
        "ldr x1, [x29, #(8*0)]\n\t"   // x0
        "ldr x2, [x29, #(8*1)]\n\t"   // x1
        "ldr x3, [x29, #(8*2)]\n\t"   // x2
        "ldr x4, [x29, #(8*3)]\n\t"   // x3
        "ldr x5, [x29, #(8*4)]\n\t"   // x4
        "ldr x6, [x29, #(8*5)]\n\t"   // x5
        "ldr x7, [x29, #(8*6)]\n\t"   // x6
        "ldr x8, [x29, #(8*7)]\n\t"   // x7
        "ldr x9, [x29, #(8*8)]\n\t"   // x8
        "ldr x10, [x29, #(8*9)]\n\t"  // x9
        "ldr x11, [x29, #(8*10)]\n\t" // x10
        "ldr x12, [x29, #(8*11)]\n\t" // x11
        "ldr x13, [x29, #(8*12)]\n\t" // x12
        "ldr x14, [x29, #(8*13)]\n\t" // x13
        "ldr x15, [x29, #(8*14)]\n\t" // x14
        "ldr x16, [x29, #(8*15)]\n\t" // x15
        // Load next gadget address and jump
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

// Block exit - saves registers and returns to C
void tcti_exit_block(int reason) {
    // Save TCTI-mapped registers back to CPU state
    asm volatile(
        "str x1, [%[cpu], #(8*0)]\n\t"
        "str x2, [%[cpu], #(8*1)]\n\t"
        "str x3, [%[cpu], #(8*2)]\n\t"
        "str x4, [%[cpu], #(8*3)]\n\t"
        "str x5, [%[cpu], #(8*4)]\n\t"
        "str x6, [%[cpu], #(8*5)]\n\t"
        "str x7, [%[cpu], #(8*6)]\n\t"
        "str x8, [%[cpu], #(8*7)]\n\t"
        "str x9, [%[cpu], #(8*8)]\n\t"
        "str x10, [%[cpu], #(8*9)]\n\t"
        "str x11, [%[cpu], #(8*10)]\n\t"
        "str x12, [%[cpu], #(8*11)]\n\t"
        "str x13, [%[cpu], #(8*12)]\n\t"
        "str x14, [%[cpu], #(8*13)]\n\t"
        "str x15, [%[cpu], #(8*14)]\n\t"
        "str x16, [%[cpu], #(8*15)]\n\t"
        :
        : [cpu] "r" (NULL)  // Will be set properly in actual implementation
        : "memory"
    );

    // Handle exit reason
    // reason = 0: normal block end
    // reason = 1: branch
    // reason = 2: syscall
    // reason = 3: signal
    (void)reason;
}
