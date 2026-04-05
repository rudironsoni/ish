/*
 * Complex TCTI Gadgets for iSH aarch64
 *
 * These gadgets handle instructions that require complex operations:
 * - Load/Store with TLB translation
 * - Bitfield operations (SBFM, UBFM, BFM)
 * - System register access
 *
 * All gadgets follow the TCTI calling convention:
 *   x28 = bytecode pointer (advances through gadget stream)
 *   x29 = cpu_state pointer
 *   x27 = temporary for next gadget address
 *
 * Exit reasons:
 *   x0 = 0: Normal exit (continue to next block)
 *   x0 = 1: Syscall exit
 *   x0 = 2: Signal exit
 *   x0 = 3: Fault exit (page fault, etc.)
 *   x0 = 4: Complex instruction (handled by C)
 */

.text
.align 4

/* ============================================================================
 * Load/Store Gadgets with TLB Support
 * ============================================================================
 *
 * Bytecode format for load/store:
 *   [gadget_ldr_imm, size, Rd, Rn, imm_offset, next_gadget]
 *   [gadget_str_imm, size, Rd, Rn, imm_offset, next_gadget]
 *
 * Where:
 *   size: 0=8-bit, 1=16-bit, 2=32-bit, 3=64-bit
 *   Rd: destination register (0-30) or 31 for SP
 *   Rn: base register (0-30) or 31 for SP
 *   imm_offset: signed immediate offset
 */

// Load register with immediate offset
// Handles TLB translation inline
.global _gadget_ldr_imm
gadget_ldr_imm:
    // Save registers we'll use
    stp     x19, x20, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    
    // Load parameters from bytecode
    ldr     x19, [x28], #8          // x19 = size (0-3)
    ldr     x20, [x28], #8          // x20 = Rd
    ldr     x21, [x28], #8          // x21 = Rn
    ldr     x22, [x28], #8          // x22 = imm_offset
    ldr     x27, [x28], #8          // x27 = next gadget
    
    // Get base address from Rn
    cmp     x21, #31
    b.eq    1f                      // If Rn == 31, use SP
    // Load from cpu->x[Rn]
    add     x23, x29, #16           // x23 = &cpu->x[0]
    lsl     x24, x21, #3            // x24 = Rn * 8
    ldr     x23, [x23, x24]         // x23 = cpu->x[Rn]
    b       2f
1:
    ldr     x23, [x29, #264]        // x23 = cpu->sp
2:
    
    // Compute effective address
    add     x23, x23, x22           // x23 = base + offset
    
    // TLB lookup
    // tlb structure: entries array at offset 0, mask at offset 2048
    // entry = (addr >> 12) & mask
    // tag = addr >> 12
    // page = entries[entry].page
    // if entries[entry].tag != tag, TLB miss
    
    // For now, exit to C to handle TLB
    // TODO: Implement inline TLB lookup
    mov     x0, #4                  // Exit reason = COMPLEX
    mov     x1, x20                 // Pass Rd
    mov     x2, x23                 // Pass address
    mov     x3, x19                 // Pass size
    // Store address for C handler
    str     x23, [x29, #872]        // Store in scratch space
    
    // Restore registers
    ldp     x23, x24, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x19, x20, [sp], #16
    
    b       _tcti_exit_block

// Store register with immediate offset
.global _gadget_str_imm
gadget_str_imm:
    // Similar to LDR but writes instead of reads
    // TODO: Implement
    mov     x0, #4                  // Exit to C for now
    b       _tcti_exit_block

/* ============================================================================
 * Bitfield Operation Gadgets
 * ============================================================================
 *
 * Bytecode format:
 *   [gadget_sbfm, Rd, Rn, immr, imms, is_64bit, next_gadget]
 *   [gadget_ubfm, Rd, Rn, immr, imms, is_64bit, next_gadget]
 *
 * SBFM: Signed bitfield move (replicates sign bit)
 * UBFM: Unsigned bitfield move (zero extends)
 *
 * Algorithm:
 *   1. Rotate right by immr
 *   2. Extract bits 0 to imms
 *   3. For SBFM: sign extend from bit imms
 */

// SBFM - Signed bitfield move
.global _gadget_sbfm
gadget_sbfm:
    // Load parameters
    ldr     x0, [x28], #8           // x0 = Rd
    ldr     x1, [x28], #8           // x1 = Rn
    ldr     x2, [x28], #8           // x2 = immr (rotate amount)
    ldr     x3, [x28], #8           // x3 = imms (field width - 1)
    ldr     x4, [x28], #8           // x4 = is_64bit
    ldr     x27, [x28], #8          // x27 = next gadget
    
    // Get source value from Rn
    cmp     x1, #31
    b.eq    1f
    add     x5, x29, #16
    lsl     x6, x1, #3
    ldr     x5, [x5, x6]            // x5 = cpu->x[Rn]
    b       2f
1:
    ldr     x5, [x29, #264]         // x5 = cpu->sp (XZR for source)
    mov     x5, #0
2:
    
    // Apply bitfield operation
    // Step 1: Rotate right by immr
    neg     x6, x2                  // x6 = -immr
    cmp     x4, #0
    b.eq    3f
    // 64-bit rotate
    ror     x5, x5, x2
    b       4f
3:
    // 32-bit rotate (mask to 32 bits first)
    and     x5, x5, #0xFFFFFFFF
    ror     w5, w5, w2
4:
    
    // Step 2: Extract field and sign extend
    // Create mask: (1 << (imms + 1)) - 1
    add     x6, x3, #1              // x6 = imms + 1
    mov     x7, #1
    lsl     x7, x7, x6              // x7 = 1 << (imms + 1)
    sub     x7, x7, #1              // x7 = mask
    
    and     x5, x5, x7              // x5 = value & mask
    
    // Step 3: Sign extend if bit imms is set
    lsl     x6, x5, #1
    lsl     x6, x6, x3              // Shift left to check sign bit
    cmp     x6, #0
    b.ge    5f                      // If positive, no extension needed
    
    // Sign extend: set all bits above imms
    neg     x6, x7                  // x6 = ~mask (bits above field)
    sub     x6, x6, #1
    orr     x5, x5, x6              // x5 = value | extension
5:
    
    // Store result to Rd
    cmp     x0, #31
    b.eq    6f
    add     x6, x29, #16
    lsl     x7, x0, #3
    str     x5, [x6, x7]            // cpu->x[Rd] = result
    b       7f
6:
    str     x5, [x29, #264]         // cpu->sp = result
7:
    
    // Continue to next gadget
    br      x27

// UBFM - Unsigned bitfield move
.global _gadget_ubfm
gadget_ubfm:
    // Similar to SBFM but without sign extension
    ldr     x0, [x28], #8           // x0 = Rd
    ldr     x1, [x28], #8           // x1 = Rn
    ldr     x2, [x28], #8           // x2 = immr
    ldr     x3, [x28], #8           // x3 = imms
    ldr     x4, [x28], #8           // x4 = is_64bit
    ldr     x27, [x28], #8          // x27 = next gadget
    
    // Get source value
    cmp     x1, #31
    b.eq    1f
    add     x5, x29, #16
    lsl     x6, x1, #3
    ldr     x5, [x5, x6]
    b       2f
1:
    mov     x5, #0
2:
    
    // Rotate and mask (no sign extension)
    cmp     x4, #0
    b.eq    3f
    ror     x5, x5, x2
    b       4f
3:
    and     x5, x5, #0xFFFFFFFF
    ror     w5, w5, w2
4:
    
    // Mask to field width
    add     x6, x3, #1
    mov     x7, #1
    lsl     x7, x7, x6
    sub     x7, x7, #1
    and     x5, x5, x7
    
    // Store result
    cmp     x0, #31
    b.eq    5f
    add     x6, x29, #16
    lsl     x7, x0, #3
    str     x5, [x6, x7]
    b       6f
5:
    str     x5, [x29, #264]
6:
    
    br      x27

/* ============================================================================
 * CBZ/CBNZ Gadgets (Compare and Branch)
 * ============================================================================
 *
 * Bytecode format:
 *   [gadget_cbz, reg_idx, target_pc, fallthrough_pc, is_cbnz, next_gadget]
 *
 * If (is_cbnz == 0 and x[reg_idx] == 0) or
 *    (is_cbnz == 1 and x[reg_idx] != 0):
 *    Branch to target_pc
 * Else:
 *    Fall through to fallthrough_pc
 */

.global _gadget_cbz
gadget_cbz:
    // Load parameters
    ldr     x0, [x28], #8           // x0 = register index
    ldr     x1, [x28], #8           // x1 = target PC if branch taken
    ldr     x2, [x28], #8           // x2 = fallthrough PC
    ldr     x3, [x28], #8           // x3 = is_cbnz (0=CBZ, 1=CBNZ)
    ldr     x27, [x28], #8          // x27 = next gadget (unused)
    
    // Get register value
    cmp     x0, #31
    b.eq    1f                      // XZR is always 0
    add     x4, x29, #16            // x4 = &cpu->x[0]
    lsl     x5, x0, #3              // x5 = reg_idx * 8
    ldr     x4, [x4, x5]            // x4 = cpu->x[reg_idx]
    b       2f
1:
    mov     x4, #0                  // XZR = 0
2:
    
    // Test condition
    cmp     x4, #0
    cset    x5, eq                  // x5 = 1 if x[reg] == 0
    
    // For CBNZ, invert the condition
    cmp     x3, #0
    b.eq    3f                      // CBZ: branch if x5 == 1
    eor     x5, x5, #1              // CBNZ: branch if x5 == 0
3:
    
    // Determine branch target
    cmp     x5, #0
    csel    x6, x1, x2, ne          // x6 = target if branch, else fallthrough
    
    // Store target PC
    str     x6, [x29, #272]         // cpu->pc = target
    
    // Exit with normal exit reason
    mov     x0, #0
    b       _tcti_exit_block

/* ============================================================================
 * System Register Access
 * ============================================================================
 */

// MRS/MSR live in gadgets_memory.c as bytecode-driven helpers.

/* ============================================================================
 * System Call
 * ============================================================================
 */

// SVC - Supervisor call (syscall)
.global _gadget_svc
gadget_svc:
    // Exit with syscall reason, C code will handle the syscall
    mov     x0, #1                  // SYSCALL exit
    b       _tcti_exit_block

/* ============================================================================
 * NOP
 * ============================================================================
 */

// NOP - No operation
.global _gadget_nop
gadget_nop:
    // Just continue to next gadget
    ldr     x27, [x28], #8          // Load next gadget
    br      x27

/* ============================================================================
 * Branch
 * ============================================================================
 */

// B - Unconditional branch
.global _gadget_b
gadget_b:
    ldr     x0, [x28], #8           // x0 = target PC
    ldr     x17, [x28], #8          // x17 = is_link
    ldr     x18, [x28], #8          // x18 = fallthrough PC
    cmp     x17, #0
    b.eq    1f
    str     x18, [x29, #256]        // cpu->x[30] = return PC
1:
    str     x0, [x29, #272]         // cpu->pc = target
    mov     x0, #0                  // Normal exit
    b       _tcti_exit_block

// B.cond - Conditional branch
.global _gadget_bcond
gadget_bcond:
    // Load parameters
    ldr     x0, [x28], #8           // x0 = target PC
    ldr     x1, [x28], #8           // x1 = fallthrough PC
    ldr     x2, [x28], #8           // x2 = condition code
    ldr     x3, [x29, #288]         // x3 = cpu->pstate (load NZCV flags)
    ldr     x27, [x28], #8          // x27 = next gadget

    // For now, exit to C to handle condition evaluation
    // This could be optimized in the future with inline condition checks
    mov     x0, #4                  // COMPLEX exit - C handles condition
    b       _tcti_exit_block
