/*
 * Test to verify pstate offset and initial value
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>

#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "emu/mmu.h"
#include "emu/tlb.h"

int main(void) {
    struct cpu_state cpu;
    
    printf("Checking cpu_state layout...\n\n");
    
    printf("offsetof(mmu) = %zu\n", offsetof(struct cpu_state, mmu));
    printf("offsetof(cycle) = %zu\n", offsetof(struct cpu_state, cycle));
    printf("offsetof(x) = %zu\n", offsetof(struct cpu_state, x));
    printf("offsetof(sp) = %zu\n", offsetof(struct cpu_state, sp));
    printf("offsetof(pc) = %zu\n", offsetof(struct cpu_state, pc));
    printf("offsetof(pstate) = %zu\n", offsetof(struct cpu_state, pstate));
    printf("offsetof(tcti_exit_reason) = %zu\n", offsetof(struct cpu_state, tcti_exit_reason));
    
    printf("\nInitializing cpu_state to zero...\n");
    memset(&cpu, 0, sizeof(cpu));
    
    printf("cpu.pstate after memset = 0x%llx\n", (unsigned long long)cpu.pstate);
    
    cpu.pstate = 0;
    printf("cpu.pstate after explicit zero = 0x%llx\n", (unsigned long long)cpu.pstate);
    
    // Set pstate to a known pattern
    cpu.pstate = 0x60000000;  // Z=1, C=1
    printf("cpu.pstate after setting 0x60000000 = 0x%llx\n", (unsigned long long)cpu.pstate);
    
    // Check actual memory at offset 280
    uint64_t *pstate_ptr = (uint64_t *)((char *)&cpu + 280);
    printf("Value at offset 280 = 0x%llx\n", (unsigned long long)*pstate_ptr);
    
    return 0;
}
