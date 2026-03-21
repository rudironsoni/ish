/*
 * Entry/exit gadgets for TCTI
 * 
 * These functions manage the transition between C code and TCTI execution.
 * They handle register save/restore and set up the TCTI execution environment.
 */

#include "asbestos/aarch64/gadgets_tcti.h"
#include "emu/aarch64/cpu.h"

// ============================================================================
// BLOCK ENTRY
// ============================================================================
// 
// Called from C with:
//   x0 = pointer to gadget array (tcti_gadget_t*)
//   x1 = pointer to cpu_state
//
// Sets up:
//   x28 = gadget stream pointer (advances as gadgets execute)
//   x29 = cpu state pointer (used by gadgets to access memory-backed regs)
//   x1-x16 = TCTI-mapped guest registers x0-x15 loaded from cpu state
//
// Then jumps to first gadget, which chains to subsequent gadgets via epilogue.
//
// CRITICAL: We must use x17/x18 as temps since they're caller-saved.
// Guest x16-x17 are stored in memory and must not be corrupted.

void tcti_entry_block(void) {
    asm volatile(
        // Input: x0 = gadgets array, x1 = cpu state
        // Use x17/x18 as temps (they're caller-saved in ARM64 ABI)
        // x18 is reserved as platform register, use x17 only
        
        "mov x17, x1\n\t"              // x17 = cpu state (temp)
        "mov x28, x0\n\t"              // x28 = gadget stream
        "mov x29, x17\n\t"             // x29 = cpu state
        
        // Load TCTI-mapped registers from CPU state
        // Guest x[n] is at offset (n * 8) in cpu_state
        // Host x[n+1] holds guest x[n]
        "ldr x1, [x29, #0]\n\t"        // Load guest x0 -> host x1
        "ldr x2, [x29, #8]\n\t"        // Load guest x1 -> host x2
        "ldr x3, [x29, #16]\n\t"       // Load guest x2 -> host x3
        "ldr x4, [x29, #24]\n\t"       // Load guest x3 -> host x4
        "ldr x5, [x29, #32]\n\t"       // Load guest x4 -> host x5
        "ldr x6, [x29, #40]\n\t"       // Load guest x5 -> host x6
        "ldr x7, [x29, #48]\n\t"       // Load guest x6 -> host x7
        "ldr x8, [x29, #56]\n\t"       // Load guest x7 -> host x8
        "ldr x9, [x29, #64]\n\t"       // Load guest x8 -> host x9
        "ldr x10, [x29, #72]\n\t"      // Load guest x9 -> host x10
        "ldr x11, [x29, #80]\n\t"      // Load guest x10 -> host x11
        "ldr x12, [x29, #88]\n\t"      // Load guest x11 -> host x12
        "ldr x13, [x29, #96]\n\t"      // Load guest x12 -> host x13
        "ldr x14, [x29, #104]\n\t"     // Load guest x13 -> host x14
        "ldr x15, [x29, #112]\n\t"     // Load guest x14 -> host x15
        "ldr x16, [x29, #120]\n\t"     // Load guest x15 -> host x16
        // x17 = temp (was cpu ptr), will be overwritten by first gadget
        // x18 = reserved platform register, left unchanged
        // x29 = cpu state (needed for memory access)
        
        // Load first gadget address and jump to it
        // Gadgets use epilogue: ldr x17, [x28], #8; br x17
        // We use x17 for temp since it's caller-saved
        "ldr x17, [x28], #8\n\t"
        "br x17\n\t"
    );
}

// ============================================================================
// BLOCK EXIT
// ============================================================================
//
// Called as the final gadget in a block (via BL or direct call).
// Saves TCTI-mapped registers back to CPU state and returns to C.

void tcti_exit_block(int reason) {
    // Save TCTI-mapped registers back to CPU state
    // x29 still points to cpu_state
    // We can use x17 as temp here since we're exiting
    asm volatile(
        "str x1, [x29, #0]\n\t"         // Save host x1 -> guest x0
        "str x2, [x29, #8]\n\t"         // Save host x2 -> guest x1
        "str x3, [x29, #16]\n\t"        // Save host x3 -> guest x2
        "str x4, [x29, #24]\n\t"        // Save host x4 -> guest x3
        "str x5, [x29, #32]\n\t"        // Save host x5 -> guest x4
        "str x6, [x29, #40]\n\t"        // Save host x6 -> guest x5
        "str x7, [x29, #48]\n\t"        // Save host x7 -> guest x6
        "str x8, [x29, #56]\n\t"        // Save host x8 -> guest x7
        "str x9, [x29, #64]\n\t"        // Save host x9 -> guest x8
        "str x10, [x29, #72]\n\t"       // Save host x10 -> guest x9
        "str x11, [x29, #80]\n\t"       // Save host x11 -> guest x10
        "str x12, [x29, #88]\n\t"       // Save host x12 -> guest x11
        "str x13, [x29, #96]\n\t"       // Save host x13 -> guest x12
        "str x14, [x29, #104]\n\t"      // Save host x14 -> guest x13
        "str x15, [x29, #112]\n\t"      // Save host x15 -> guest x14
        "str x16, [x29, #120]\n\t"      // Save host x16 -> guest x15
        // Note: Guest x16-x30 remain in memory, unchanged
        :
        :
        : "memory"
    );
    
    // The reason parameter tells us why we exited:
    // 0 = normal block end
    // 1 = branch taken
    // 2 = syscall
    // 3 = fault/exception
    // For now, we just return. The caller can check cpu->pc to see if it changed.
    (void)reason;
}
