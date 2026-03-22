/*
 * TCTI Entry/Exit - Following QEMU's proven design
 * 
 * Register mapping:
 *   x1-x15  -> Guest x0-x14 (TCTI-mapped)
 *   x17     -> Temp (used by load/store sequences)
 *   x18     -> Temp (used by load/store sequences)  
 *   x24     -> TCTI temp
 *   x25     -> Saved IP during call
 *   x26     -> TCTI temp
 *   x27     -> TCTI temp
 *   x28     -> Bytecode pointer (advances through gadget stream)
 *   x29     -> CPU state pointer
 *   x30     -> Link register
 */

.global _tcti_entry_block
.global tcti_entry_block
_tcti_entry_block:
tcti_entry_block:
    // Entry: x0 = gadget array, x1 = cpu_state
    // Setup TCTI environment
    mov     x28, x0                 // x28 = bytecode pointer (gadget stream)
    mov     x29, x1                 // x29 = cpu state
    
    // Load guest x0-x14 into host x1-x15
    // x[0] at offset 16, x[1] at 24, etc.
    ldr     x1, [x29, #16]          // Guest x0 -> host x1
    ldr     x2, [x29, #24]          // Guest x1 -> host x2
    ldr     x3, [x29, #32]          // Guest x2 -> host x3
    ldr     x4, [x29, #40]          // Guest x3 -> host x4
    ldr     x5, [x29, #48]          // Guest x4 -> host x5
    ldr     x6, [x29, #56]          // Guest x5 -> host x6
    ldr     x7, [x29, #64]          // Guest x6 -> host x7
    ldr     x8, [x29, #72]          // Guest x7 -> host x8
    ldr     x9, [x29, #80]          // Guest x8 -> host x9
    ldr     x10, [x29, #88]         // Guest x9 -> host x10
    ldr     x11, [x29, #96]         // Guest x10 -> host x11
    ldr     x12, [x29, #104]        // Guest x11 -> host x12
    ldr     x13, [x29, #112]        // Guest x12 -> host x13
    ldr     x14, [x29, #120]        // Guest x13 -> host x14
    ldr     x15, [x29, #128]        // Guest x14 -> host x15
    
    // Guest x15-x30 and SP stay in memory
    // Jump to first gadget
    ldr     x27, [x28], #8          // Load first gadget, advance bytecode
    br      x27                     // Execute first gadget

// TCTI epilogue - each gadget ends with this
.global _tcti_epilogue
.global tcti_epilogue
_tcti_epilogue:
tcti_epilogue:
    ldr     x27, [x28], #8          // Load next gadget from bytecode
    br      x27                     // Jump to it

// C call prologue - save all TCTI registers before calling C
.global _tcti_c_call_prologue
.global tcti_c_call_prologue
_tcti_c_call_prologue:
tcti_c_call_prologue:
    stp     x25, x26, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    stp     x17, x18, [sp, #-16]!
    stp     x15, x16, [sp, #-16]!
    stp     x13, x14, [sp, #-16]!
    stp     x11, x12, [sp, #-16]!
    stp     x9,  x10, [sp, #-16]!
    stp     x7,  x8,  [sp, #-16]!
    stp     x5,  x6,  [sp, #-16]!
    stp     x3,  x4,  [sp, #-16]!
    stp     x1,  x2,  [sp, #-16]!
    stp     x28, x30, [sp, #-16]!   // Save bytecode ptr and LR
    ret

// C call epilogue - restore all TCTI registers after C call
.global _tcti_c_call_epilogue
.global tcti_c_call_epilogue
_tcti_c_call_epilogue:
tcti_c_call_epilogue:
    ldp     x28, x30, [sp], #16     // Restore bytecode ptr and LR
    ldp     x1,  x2,  [sp], #16
    ldp     x3,  x4,  [sp], #16
    ldp     x5,  x6,  [sp], #16
    ldp     x7,  x8,  [sp], #16
    ldp     x9,  x10, [sp], #16
    ldp     x11, x12, [sp], #16
    ldp     x13, x14, [sp], #16
    ldp     x15, x16, [sp], #16
    ldp     x17, x18, [sp], #16
    ldp     x19, x20, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x23, x24, [sp], #16
    ldp     x25, x26, [sp], #16
    ret

// Exit block - save registers and return to C
.global _tcti_exit_block
.global tcti_exit_block
_tcti_exit_block:
tcti_exit_block:
    // Save guest x0-x14 from host x1-x15 back to memory
    str     x1,  [x29, #16]         // Host x1 -> guest x0
    str     x2,  [x29, #24]         // Host x2 -> guest x1
    str     x3,  [x29, #32]         // Host x3 -> guest x2
    str     x4,  [x29, #40]         // Host x4 -> guest x3
    str     x5,  [x29, #48]         // Host x5 -> guest x4
    str     x6,  [x29, #56]         // Host x6 -> guest x5
    str     x7,  [x29, #64]         // Host x7 -> guest x6
    str     x8,  [x29, #72]         // Host x8 -> guest x7
    str     x9,  [x29, #80]         // Host x9 -> guest x8
    str     x10, [x29, #88]         // Host x10 -> guest x9
    str     x11, [x29, #96]         // Host x11 -> guest x10
    str     x12, [x29, #104]        // Host x12 -> guest x11
    str     x13, [x29, #112]        // Host x13 -> guest x12
    str     x14, [x29, #120]        // Host x14 -> guest x13
    str     x15, [x29, #128]        // Host x15 -> guest x14
    
    // Guest x15-x30 and SP already in memory (never loaded to host regs)
    
    // Return to C
    ret

// Load memory-backed register (x15-x30) into temp x17
// Index 0-15 in x0 (corresponds to x15-x30)
.global _tcti_load_xreg
.global tcti_load_xreg
_tcti_load_xreg:
tcti_load_xreg:
    // x0 = register index (0=x15, 1=x16, ..., 15=x30)
    // Load x[15 + index] into x17
    add     x0, x0, #15             // Convert to actual register number
    add     x17, x29, #16           // Base of x[] array
    add     x17, x17, x0, lsl #3    // x17 = &cpu->x[15+index]
    ldr     x17, [x17]              // Load value
    ret

// Store temp x17 to memory-backed register (x15-x30)
// Index 0-15 in x0 (corresponds to x15-x30)
.global _tcti_store_xreg
.global tcti_store_xreg
_tcti_store_xreg:
tcti_store_xreg:
    add     x0, x0, #15             // Convert to actual register number
    add     x27, x29, #16           // Base of x[] array
    add     x27, x27, x0, lsl #3    // x27 = &cpu->x[15+index]
    str     x17, [x27]              // Store value
    ret

// Load SP into temp x18
.global _tcti_load_sp
.global tcti_load_sp
_tcti_load_sp:
tcti_load_sp:
    ldr     x18, [x29, #264]        // offsetof(cpu_state, sp) = 264
    ret

// Store temp x18 to SP
.global _tcti_store_sp
.global tcti_store_sp
_tcti_store_sp:
tcti_store_sp:
    str     x18, [x29, #264]
    ret
