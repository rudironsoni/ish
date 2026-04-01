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
    // Save x0 and x1 (cpu and block pointers) on stack - these must be preserved
    stp     x0, x1, [sp, #-16]!
    
    // Preserve the caller's callee-saved state and Apple platform register x18.
    stp     x17, x18, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!

    // Setup TCTI environment FIRST (before clobbering x0)
    // x0 still contains gadgets, x1 still contains cpu_state
    mov     x28, x0                 // x28 = bytecode pointer (gadget stream)
    mov     x29, x1                 // x29 = cpu state
    
    // =========================================================================
    // TCTI ENTRY BOUNDARY TRACING - Spill-First Instrumentation
    // =========================================================================
    // Capture exact values at TCTI entry for forensic analysis
    // 
    // Register state at this point:
    //   x0 = gadgets (bytecode stream base)
    //   x1 = cpu_state pointer
    //   x28 = bytecode pointer (copy of x0)
    //   x29 = cpu_state (copy of x1)
    //   x27 = undefined (will be loaded from bytecode)
    //
    // Trace sequence:
    //   1. TCTI_ENTRY_X28_BEFORE: x28 value before any loads
    //   2. TCTI_ENTRY_QWORD0: [x28] - first qword of bytecode
    //   3. Load x27 from [x28], advance x28
    //   4. TCTI_ENTRY_X27_AFTER: x27 after first load
    //   5. TCTI_ENTRY_X28_AFTER: x28 after first load (bytecode advanced)
    //   6. TCTI_ENTRY_QWORD1: [x28] - second qword of bytecode
    //
    // Note: x27 = gadget to execute, x28 advanced by 8
    // =========================================================================
    
    // Save registers for trace call (preserve x28, x29)
    stp     x27, x28, [sp, #-16]!
    
    // Trace TCTI_ENTRY_X28_BEFORE
    mov     x0, x28
    // Store to global variable for crash report visibility
    adrp    x18, _tcti_last_x28_before@PAGE
    add     x18, x18, _tcti_last_x28_before@PAGEOFF
    str     x28, [x18]
    bl      _trace_emit_tcti_entry_x28_before
    
    // Reload x28 (clobbered by call)
    ldr     x28, [sp, #8]
    
    // Load qword0 from [x28] for tracing
    ldr     x0, [x28]
    // Store to global variable for crash report visibility
    adrp    x18, _tcti_last_qword0@PAGE
    add     x18, x18, _tcti_last_qword0@PAGEOFF
    str     x0, [x18]
    bl      _trace_emit_tcti_entry_qword0
    
    // Load x27 from [x28], advance x28
    // This is the first gadget pointer we're about to execute
    ldr     x27, [x28], #8
    
    // Trace x27 after load
    mov     x0, x27
    // Store to global variable for crash report visibility
    adrp    x18, _tcti_last_x27_after@PAGE
    add     x18, x18, _tcti_last_x27_after@PAGEOFF
    str     x27, [x18]
    bl      _trace_emit_tcti_entry_x27_after
    
    // Trace x28 after load (advanced by 8)
    mov     x0, x28
    // Store to global variable for crash report visibility
    adrp    x18, _tcti_last_x28_after@PAGE
    add     x18, x18, _tcti_last_x28_after@PAGEOFF
    str     x28, [x18]
    bl      _trace_emit_tcti_entry_x28_after
    
    // Load and trace qword1 (current [x28])
    ldr     x0, [x28]
    // Store to global variable for crash report visibility
    adrp    x18, _tcti_last_qword1@PAGE
    add     x18, x18, _tcti_last_qword1@PAGEOFF
    str     x0, [x18]
    bl      _trace_emit_tcti_entry_qword1
    
    // Restore x28 from stack (x27 contains the gadget pointer, don't restore it!)
    ldr     x28, [sp, #8]
    add     sp, sp, #16
    
    // Restore NZCV after trace calls (may have clobbered flags)
    ldr     x0, [x29, #280]         // Restore saved NZCV/PSTATE flags
    msr     nzcv, x0
    
    // Store stack canary using x0 (now safe to clobber)
    mov     x0, #0xABCD
    str     x0, [sp, #-16]!
    
    // Load guest x0-x15 into host x1-x16
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
    ldr     x16, [x29, #136]        // Guest x15 -> host x16
    
    // Guest x15-x30 and SP stay in memory

    // Guard: Check if first gadget pointer is valid before jumping
    cbz     x27, tcti_bad_gadget    // If x27 is NULL, exit gracefully
    // Jump to first gadget (already loaded into x27 during tracing)
    br      x27                     // Execute first gadget

tcti_bad_gadget:
    // Exit with error - invalid gadget pointer (NULL)
    mov     x0, #-1                 // Exit code: invalid gadget
    b       tcti_exit_block         // Go to exit

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
    str     x28, [sp, #-16]!         // Save bytecode ptr (x0 is clobbered by C return value)
    ret

// C call epilogue - restore all TCTI registers after C call
.global _tcti_c_call_epilogue
.global tcti_c_call_epilogue
_tcti_c_call_epilogue:
tcti_c_call_epilogue:
    ldr     x28, [sp], #16          // Restore bytecode ptr only (x0 has C return value)
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
// On entry: x0 = exit reason (TCTI_EXIT_*)
.global _tcti_exit_block
.global tcti_exit_block
_tcti_exit_block:
tcti_exit_block:
    // Save exit reason to cpu_state
    // tcti_exit_reason is at offset 860 (calculated from structure layout)
    str     w0,  [x29, #860]        // Store exit reason to cpu->tcti_exit_reason (32-bit int)
    
    // CRITICAL: Save NZCV immediately before any flag-setting instruction
    // This prevents debug logic (cmp below) from clobbering guest flags
    mrs     x17, nzcv
    str     x17, [x29, #280]        // Save NZCV back to cpu->pstate

    // Independent repeated-block probe outside gadget-local diagnostic sinks.
    // GUARD: Check if _tcti_asm_probe is properly linked before writing
    adrp    x18, _tcti_asm_probe@PAGE
    add     x18, x18, _tcti_asm_probe@PAGEOFF
    cbz     x18, 2f                 // Skip probe if symbol resolved to NULL
    ldr     x17, [x29, #272]        // cpu->pc
    str     x17, [x18, #0]
    str     x29, [x18, #8]
    str     x3,  [x18, #16]
    str     x6,  [x18, #24]
    mrs     x17, nzcv
    str     x17, [x18, #32]
    mov     w17, #1
    strb    w17, [x18, #40]
2:
    
    cmp     x0,  #16
    b.lo    1f
    str     x28, [x29, #816]        // Debug: current bytecode stream pointer for bad exits
    str     w27, [x29, #856]        // Debug: low 32 bits of x27 for bad exits
1:
    
    // Save guest x0-x15 from host x1-x16 back to memory
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
    str     x16, [x29, #136]        // Host x16 -> guest x15
    // Note: NZCV was saved at the top of this function before any flag-setting instructions

    // Guest x16-x30 and SP already in memory (never loaded to host regs)
    
    // Debug: Store x29 value before restoring from stack to check for corruption
    mov     x17, x29
    
    // Restore the caller's preserved state and return to C.
    // Must be in REVERSE order of entry
    // The canary was pushed AFTER x29/x30, so it's at the top of stack
    // We need to skip over it to get to the registers
    add     sp, sp, #16             // Skip canary slot (don't check, just skip)
    ldp     x29, x30, [sp], #16     // Pushed 7th (oldest callee-saved)
    ldp     x27, x28, [sp], #16     // Pushed 6th
    ldp     x25, x26, [sp], #16     // Pushed 5th
    ldp     x23, x24, [sp], #16     // Pushed 4th
    ldp     x21, x22, [sp], #16     // Pushed 3rd
    ldp     x19, x20, [sp], #16     // Pushed 2nd
    ldp     x17, x18, [sp], #16     // Pushed 1st (closest to x0/x1)
    // Restore x0 (cpu) and x1 (block) parameters that were saved on entry
    ldp     x0, x1, [sp], #16
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

.data
.balign 8

/* Global variables for last TCTI values - visible in crash reports */
.global _tcti_last_x28_before
_tcti_last_x28_before:
    .quad 0

.global _tcti_last_x27_after
_tcti_last_x27_after:
    .quad 0

.global _tcti_last_x28_after
_tcti_last_x28_after:
    .quad 0

.global _tcti_last_qword0
_tcti_last_qword0:
    .quad 0

.global _tcti_last_qword1
_tcti_last_qword1:
    .quad 0

.global _tcti_asm_probe
_tcti_asm_probe:
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .byte 0
    .balign 8
