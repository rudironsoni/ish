/*
 * Inline TLB Load/Store Gadgets for 100% TCTI
 * 
 * Based on QEMU's proven design:
 * - Fast-path TLB lookup in assembly
 * - Only TLB miss branches to C
 * - Zero overhead for cache hits
 * 
 * Register mapping during load/store:
 *   x0-x15  -> Guest x0-x14 (TCTI-mapped) - preserved
 *   x16     -> Base address calculation temp
 *   x17     -> Computed guest address
 *   x18     -> TLB entry address
 *   x24     -> Page comparator (from TLB entry)
 *   x25     -> data_minus_addr (from TLB entry)
 *   x26     -> TLB index temp
 *   x27     -> Next gadget address (epilogue)
 *   x28     -> Bytecode pointer
 *   x29     -> CPU state pointer
 */

.text
.align 4

/* ============================================================================
 * TLB Lookup Macros
 * ============================================================================
 * TLB entry structure (24 bytes):
 *   offset 0:  page (page_t) - page address for read comparison
 *   offset 8:  page_if_writable (page_t) - for write permission check
 *   offset 16: data_minus_addr (uintptr_t) - host pointer - guest address
 *
 * TLB index calculation:
 *   TLB_INDEX(addr) = (((addr >> 12) & 1023) ^ (addr >> 22))
 *   where PAGE_BITS = 12, TLB_BITS = 10, TLB_SIZE = 1024
 *
 * TLB is accessed via cpu->mem->tlb
 * cpu->mem is at offset 0 in cpu_state
 * mem->tlb.entries is at offset in struct mem
 */

/* 
 * TLB_ENTRY_OFFSET - compute TLB entry address
 * Input: x17 = guest address
 * Output: x18 = TLB entry address
 * Clobbers: x16, x26
 */
.macro tlb_compute_entry
    // TLB index = (((addr >> 12) & 1023) ^ (addr >> 22))
    lsr     x16, x17, #12           // addr >> PAGE_BITS
    and     x26, x16, #1023         // & (TLB_SIZE - 1)
    lsr     x16, x17, #22           // addr >> (PAGE_BITS + TLB_BITS)
    eor     x26, x26, x16           // XOR for final index
    
    // entry = &tlb->entries[index]
    // entries array starts at entries offset in struct tlb
    // each entry is 24 bytes
    mov     x16, #24
    mul     x26, x26, x16           // index * sizeof(tlb_entry)
    
    // Load tlb pointer from cpu->mem->tlb
    // cpu->mem is at offset 0 of cpu_state
    ldr     x18, [x29, #0]          // x18 = cpu->mem
    // tlb is at offset in mem struct - need to check actual offset
    // For now, assume tlb entries follow immediately after mem pointer
    // Actually, we need to load from mem->tlb.entries
    // Let me check the structure...
.endm

/*
 * Load with TLB - LDR (64-bit)
 * Bytecode: [gadget_ldr_tlb, Rd, Rn, imm12, next_gadget]
 * Operation: x[Rd] = *(uint64_t*)(x[Rn] + (imm12 << 3))
 * 
 * Fast path:
 *   1. Compute guest address = base + offset
 *   2. Look up TLB entry
 *   3. Compare page
 *   4. If match: compute host addr, load value, store to Rd
 *   5. Jump to next gadget
 * 
 * Slow path (miss):
 *   Call C helper tlb_handle_miss
 */
.global _gadget_ldr_tlb_64
.global gadget_ldr_tlb_64
_gadget_ldr_tlb_64:
gadget_ldr_tlb_64:
    // Load parameters from bytecode
    ldr     x16, [x28], #8          // x16 = Rd (destination register index)
    ldr     x17, [x28], #8          // x17 = Rn (base register index)
    ldr     x26, [x28], #8          // x26 = imm12 (immediate offset)
    ldr     x27, [x28], #8          // x27 = next gadget
    
    // Load base register value
    cmp     x17, #15
    b.gt    .Lbase_mem_backed       // x16-x30 are memory-backed
    // TCTI-mapped registers x0-x15 are in host x1-x16
    // Guest x[Rn] is in host x[Rn+1]
    add     x17, x17, #1
    lsl     x18, x17, #3            // offset in register file
    add     x18, x29, x18
    ldr     x17, [x18, #16]         // x17 = base value
    b       .Lbase_loaded
.Lbase_mem_backed:
    // Load from memory-backed register
    // x[16] is at offset 16 + 16*8 = 144
    sub     x18, x17, #16
    lsl     x18, x18, #3
    add     x18, x18, #144
    add     x18, x29, x18
    ldr     x17, [x18]              // x17 = base value
.Lbase_loaded:
    
    // Compute guest address = base + (imm12 << 3)
    lsl     x26, x26, #3            // imm12 * 8 for 64-bit
    add     x17, x17, x26           // x17 = guest address
    
    // TODO: Inline TLB lookup
    // For now, fall back to C (not 100% TCTI yet)
    // We need to store state, call helper, restore state
    
    // Save registers that C will clobber
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    
    // Prepare call: x0 = tlb, x1 = addr, x2 = Rd
    // Need to get tlb pointer...
    
    // For now, use the slow path via exit
    mov     x0, #3                  // TCTI_EXIT_FAULT
    b       _tcti_exit_block

/*
 * Store with TLB - STR (64-bit)
 * Bytecode: [gadget_str_tlb, Rd, Rn, imm12, next_gadget]
 * Operation: *(uint64_t*)(x[Rn] + (imm12 << 3)) = x[Rd]
 */
.global _gadget_str_tlb_64
.global gadget_str_tlb_64
_gadget_str_tlb_64:
gadget_str_tlb_64:
    // Similar to LDR but for stores
    // TODO: Implement
    mov     x0, #3                  // TCTI_EXIT_FAULT
    b       _tcti_exit_block
