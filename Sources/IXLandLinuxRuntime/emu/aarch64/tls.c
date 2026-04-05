/*
 * aarch64 TLS implementation
 */

#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/emu/aarch64/tls.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#include <string.h>

void a64_tls_init(struct cpu_state *cpu)
{
    // TPIDR_EL0 starts at 0, must be set by libc/loader
    cpu->tpidr_el0 = 0;
}

int a64_setup_tls_area(struct cpu_state *cpu, uint64_t tls_base)
{
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
int a64_handle_mrs(struct cpu_state *cpu, int Rt, uint32_t sysreg)
{
    return a64_sysreg_read(cpu, (uint16_t)sysreg, Rt) == TCTI_EXIT_NORMAL;
}

/*
 * Handle MSR (write system register)
 */
int a64_handle_msr(struct cpu_state *cpu, uint32_t sysreg, int Rt)
{
    return a64_sysreg_write(cpu, (uint16_t)sysreg, Rt) == TCTI_EXIT_NORMAL;
}
