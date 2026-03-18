/*
 * aarch64 TLS implementation
 */

#include "emu/aarch64/tls.h"
#include <string.h>

void a64_tls_init(struct cpu_state *cpu) {
    // TPIDR_EL0 starts at 0, must be set by libc/loader
    cpu->tpidr_el0 = 0;
}

int a64_setup_tls_area(struct cpu_state *cpu, uint64_t tls_base) {
    cpu->tpidr_el0 = tls_base;
    return 0;
}

/*
 * Handle MRS (read system register)
 * System register encoding:
 *   op0 = bits 19:18 (should be 3)
 *   op1 = bits 16:14
 *   CRn = bits 13:10
 *   CRm = bits 9:6
 *   op2 = bits 5:3
 */
int a64_handle_mrs(struct cpu_state *cpu, int Rt, uint32_t sysreg) {
    // Decode system register
    int op0 = (sysreg >> 18) & 0x3;
    int op1 = (sysreg >> 14) & 0x7;
    int crn = (sysreg >> 10) & 0xF;
    int crm = (sysreg >> 6) & 0xF;
    int op2 = (sysreg >> 3) & 0x7;

    // Must be op0=3 (system registers accessible from EL0)
    if (op0 != 3)
        return 0;

    // TPIDR_EL0: op1=3, CRn=13, CRm=0, op2=2
    if (op1 == 3 && crn == 13 && crm == 0 && op2 == 2) {
        if (Rt < 31) {
            cpu->x[Rt] = cpu->tpidr_el0;
        }
        return 1;
    }

    // TPIDRRO_EL0 (read-only): op1=3, CRn=13, CRm=0, op2=3
    if (op1 == 3 && crn == 13 && crm == 0 && op2 == 3) {
        if (Rt < 31) {
            cpu->x[Rt] = cpu->tpidr_el0;  // Same value on Linux
        }
        return 1;
    }

    // CNTVCT_EL0 (virtual counter - needed for gettimeofday): op1=3, CRn=14, CRm=0, op2=2
    if (op1 == 3 && crn == 14 && crm == 0 && op2 == 2) {
        // Return host time - this is a simplified version
        // Real implementation needs proper vdso/vsyscall handling
        if (Rt < 31) {
            cpu->x[Rt] = 0;  // Placeholder
        }
        return 1;
    }

    // CNTFRQ_EL0 (counter frequency): op1=3, CRn=14, CRm=0, op2=0
    if (op1 == 3 && crn == 14 && crm == 0 && op2 == 0) {
        if (Rt < 31) {
            cpu->x[Rt] = 1000000000;  // 1GHz - typical value
        }
        return 1;
    }

    // Unknown register
    return 0;
}

/*
 * Handle MSR (write system register)
 */
int a64_handle_msr(struct cpu_state *cpu, uint32_t sysreg, int Rt) {
    int op0 = (sysreg >> 18) & 0x3;
    int op1 = (sysreg >> 14) & 0x7;
    int crn = (sysreg >> 10) & 0xF;
    int crm = (sysreg >> 6) & 0xF;
    int op2 = (sysreg >> 3) & 0x7;

    if (op0 != 3)
        return 0;

    // TPIDR_EL0
    if (op1 == 3 && crn == 13 && crm == 0 && op2 == 2) {
        if (Rt < 31) {
            cpu->tpidr_el0 = cpu->x[Rt];
        } else {
            cpu->tpidr_el0 = 0;
        }
        return 1;
    }

    // DCZVA (data cache zero by VA) - treat as NOP in user mode
    if (op1 == 3 && crn == 7 && crm == 6 && op2 == 3) {
        // NOP - caches not really emulated
        return 1;
    }

    // Cache maintenance operations - treat as NOPs
    if (crn == 7) {
        // ICIVAU, IC IALLU, DC IVAC, DC CVAC, DC CVAU, DC CVAP, DC CIVAC
        // All NOPs in our emulation
        return 1;
    }

    // Memory barrier instructions
    if (crn == 11 || crn == 3) {
        // DMB, DSB, ISB - NOPs since we're single-threaded per core
        return 1;
    }

    return 0;
}
