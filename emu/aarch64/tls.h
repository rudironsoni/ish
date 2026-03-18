#ifndef AARCH64_TLS_H
#define AARCH64_TLS_H

/*
 * aarch64 Thread Local Storage implementation
 *
 * aarch64 uses TPIDR_EL0 system register for the thread pointer
 * This replaces x86's fs/gs segment registers
 */

#include "misc.h"
#include "emu/aarch64/cpu.h"

// TPIDR_EL0 encoding
#define A64_SYS_TPIDR_EL0 0x5E82  // op0=3, op1=3, CRn=13, CRm=0, op2=2

// System registers for TLS
#define A64_SYSREG_TPIDR_EL0   0xd0510202  // Full encoding
#define A64_SYSREG_TPIDRRO_EL0 0xd0510203  // Read-only TPIDR

/*
 * Read TPIDR_EL0 (MRS instruction)
 * Returns the current thread pointer
 */
static inline uint64_t a64_read_tpidr_el0(struct cpu_state *cpu) {
    return cpu->tpidr_el0;
}

/*
 * Write TPIDR_EL0 (MSR instruction)
 * Sets the thread pointer
 */
static inline void a64_write_tpidr_el0(struct cpu_state *cpu, uint64_t val) {
    cpu->tpidr_el0 = val;
}

/*
 * Initialize TLS for a new thread
 * Called when creating a new task/thread
 */
void a64_tls_init(struct cpu_state *cpu);

/*
 * Setup TLS area for the initial process
 * Matches Linux's set_thread_area behavior for aarch64
 */
int a64_setup_tls_area(struct cpu_state *cpu, uint64_t tls_base);

/*
 * Get TLS offset for a symbol
 * Used for implementing TLS access sequences
 */
static inline uint64_t a64_tls_offset(struct cpu_state *cpu, uint64_t offset) {
    return cpu->tpidr_el0 + offset;
}

/*
 * Handle MRS instruction for system registers
 * Returns 1 if handled, 0 if unknown register
 */
int a64_handle_mrs(struct cpu_state *cpu, int Rt, uint32_t sysreg);

/*
 * Handle MSR instruction for system registers
 * Returns 1 if handled, 0 if unknown register
 */
int a64_handle_msr(struct cpu_state *cpu, uint32_t sysreg, int Rt);

#endif
