/*
 * aarch64 Instruction Fetch
 *
 * Stage [1] FETCH: Guest PC -> 32-bit A64 instruction word
 *
 * Contract: Deterministic fetch from guest memory via TLB
 * Owner: Sources/IXLandLinuxRuntime/emu/aarch64/fetch.c
 */

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/fetch.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/errno.h>

int a64_fetch_insn(struct cpu_state *cpu, struct tlb *tlb, uint64_t pc, uint32_t *insn)
{
    if ((pc & 3) != 0) {
        cpu->fault_addr = pc;
        cpu->fault_was_write = 0;
        return _EFAULT;
    }

    // Use iSH's TLB for fast lookup
    void *ptr = __tlb_read_ptr(tlb, pc);
    if (ptr == NULL) {
        // TLB miss - use slow path
        ptr = tlb_handle_miss(tlb, pc, MEM_READ);
        if (ptr == NULL) {
            cpu->fault_addr = tlb->segfault_addr;
            cpu->fault_was_write = 0;
            return _EFAULT;
        }
    }

    *insn = *(uint32_t *)ptr;
    return 0;
}
