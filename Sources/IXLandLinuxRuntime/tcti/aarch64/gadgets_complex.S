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

#include "cpu-offsets.h"

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
    add     x23, x29, #CPU_OFFSET_x // x23 = &cpu->x[0]
    lsl     x24, x21, #3            // x24 = Rn * 8
    ldr     x23, [x23, x24]         // x23 = cpu->x[Rn]
    b       2f
1:
    ldr     x23, [x29, #CPU_OFFSET_sp] // x23 = cpu->sp
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
    str     x23, [x29, #CPU_OFFSET_exclusive_addr] // Store in scratch space
    
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
 *   [gadget_bfm,  Rd, Rn, immr, imms, is_64bit, next_gadget]
 *   [gadget_ubfm, Rd, Rn, immr, imms, is_64bit, next_gadget]
 */

.macro bitfield_gadget subtype
    ldr     x19, [x28], #8          // rd
    ldr     x20, [x28], #8          // rn
    ldr     x21, [x28], #8          // immr
    ldr     x22, [x28], #8          // imms
    ldr     x23, [x28], #8          // is_64bit
    stp     x1, x2, [x29, #16]
    stp     x3, x4, [x29, #32]
    stp     x5, x6, [x29, #48]
    stp     x7, x8, [x29, #64]
    stp     x9, x10, [x29, #80]
    stp     x11, x12, [x29, #96]
    stp     x13, x14, [x29, #112]
    stp     x15, x16, [x29, #128]
    bl      _tcti_c_call_prologue
    mov     x0, x29
    mov     x1, x19
    mov     x2, x20
    mov     x3, x21
    mov     x4, x22
    mov     x5, x23
    mov     x6, #\subtype
    bl      _tcti_bitfield_helper
    bl      _tcti_c_call_epilogue
    ldp     x1, x2, [x29, #16]
    ldp     x3, x4, [x29, #32]
    ldp     x5, x6, [x29, #48]
    ldp     x7, x8, [x29, #64]
    ldp     x9, x10, [x29, #80]
    ldp     x11, x12, [x29, #96]
    ldp     x13, x14, [x29, #112]
    ldp     x15, x16, [x29, #128]
    cmp     x19, #16
    b.hs    1f
    mov     x26, x19
    bl      _tcti_sync_hot_reg_from_cpu
1:
    ldr     x27, [x28], #8
    br      x27
.endm

.global _gadget_sbfm
gadget_sbfm:
    bitfield_gadget 11

.global _gadget_bfm
gadget_bfm:
    bitfield_gadget 12

.global _gadget_ubfm
gadget_ubfm:
    bitfield_gadget 13

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
    add     x4, x29, #CPU_OFFSET_x  // x4 = &cpu->x[0]
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
    str     x6, [x29, #CPU_OFFSET_pc] // cpu->pc = target
    
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
    ldr x27, [x28], #8     // Load next gadget
    br x27

/* ============================================================================
 * PC Advance - Updates cpu->pc for fallthrough execution
 * ============================================================================
 */
// PC advance - Store next PC to cpu_state for fallthrough
.global _gadget_pc_advance
gadget_pc_advance:
    // Load next PC from bytecode
    ldr x0, [x28], #8      // x0 = next PC
    // Store to cpu->pc via cpu state pointer in x29
    str x0, [x29, #CPU_OFFSET_pc]  // cpu->pc = next PC
    // Continue to next gadget
    ldr x27, [x28], #8     // Load next gadget
    br x27

/* ============================================================================
 * Branch
 * ============================================================================
 */

// B - Unconditional branch
.global _gadget_b
gadget_b:
    ldr     x0, [x28], #8           // x0 = target PC
    ldr     x17, [x28], #8          // x17 = is_link
    ldr     x19, [x28], #8          // x19 = fallthrough PC
    cmp     x17, #0
    b.eq    1f
    str     x19, [x29, #CPU_OFFSET_x30] // cpu->x[30] = return PC
1:
    str     x0, [x29, #CPU_OFFSET_pc] // cpu->pc = target
    mov     x0, #0                  // Normal exit
    b       _tcti_exit_block

// B.cond - Conditional branch
.global _gadget_bcond
gadget_bcond:
    // Load parameters
    ldr     x0, [x28], #8           // x0 = target PC
    ldr     x1, [x28], #8           // x1 = fallthrough PC
    ldr     x2, [x28], #8           // x2 = condition code
    ldr     x3, [x29, #CPU_OFFSET_pstate] // x3 = cpu->pstate (load NZCV flags)
    ldr     x27, [x28], #8          // x27 = next gadget

    // For now, exit to C to handle condition evaluation
    // This could be optimized in the future with inline condition checks
    mov     x0, #4                  // COMPLEX exit - C handles condition
    b       _tcti_exit_block
