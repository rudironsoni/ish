/*
 * aarch64 CPU state offsets for TCTI gadgets
 * 
 * This file defines the offsets of fields in struct cpu_state
 * that are used by assembly gadgets to access registers.
 */

#include "emu/aarch64/cpu.h"
#include <stddef.h>

// Define offsets for use by assembly code
// These are used by gadgets to access CPU state fields

#define DEFINE_OFFSET(name, field) \
    const size_t CPU_OFFSET_##name = offsetof(struct cpu_state, field)

// General purpose registers x0-x30
DEFINE_OFFSET(X0, x[0]);
DEFINE_OFFSET(X1, x[1]);
DEFINE_OFFSET(X2, x[2]);
DEFINE_OFFSET(X3, x[3]);
DEFINE_OFFSET(X4, x[4]);
DEFINE_OFFSET(X5, x[5]);
DEFINE_OFFSET(X6, x[6]);
DEFINE_OFFSET(X7, x[7]);
DEFINE_OFFSET(X8, x[8]);
DEFINE_OFFSET(X9, x[9]);
DEFINE_OFFSET(X10, x[10]);
DEFINE_OFFSET(X11, x[11]);
DEFINE_OFFSET(X12, x[12]);
DEFINE_OFFSET(X13, x[13]);
DEFINE_OFFSET(X14, x[14]);
DEFINE_OFFSET(X15, x[15]);
DEFINE_OFFSET(X16, x[16]);
DEFINE_OFFSET(X17, x[17]);
DEFINE_OFFSET(X18, x[18]);
DEFINE_OFFSET(X19, x[19]);
DEFINE_OFFSET(X20, x[20]);
DEFINE_OFFSET(X21, x[21]);
DEFINE_OFFSET(X22, x[22]);
DEFINE_OFFSET(X23, x[23]);
DEFINE_OFFSET(X24, x[24]);
DEFINE_OFFSET(X25, x[25]);
DEFINE_OFFSET(X26, x[26]);
DEFINE_OFFSET(X27, x[27]);
DEFINE_OFFSET(X28, x[28]);
DEFINE_OFFSET(X29, x[29]);
DEFINE_OFFSET(X30, x[30]);

// Stack pointer and program counter
DEFINE_OFFSET(SP, sp);
DEFINE_OFFSET(PC, pc);

// PSTATE (flags)
DEFINE_OFFSET(PSTATE, pstate);
DEFINE_OFFSET(NZCV, pstate);

// Floating point registers (if used)
// DEFINE_OFFSET(V0, vregs[0]);
// ...

// System registers
DEFINE_OFFSET(TPIDR_EL0, tpidr_el0);

// MMU pointer
DEFINE_OFFSET(MMU, mmu);

// Execution context (PR 1 optimization)
DEFINE_OFFSET(EXEC_CTX, exec_ctx);

// Interrupt state
DEFINE_OFFSET(TRAPNO, trapno);
DEFINE_OFFSET(POKED, _poked);

// Fault info
DEFINE_OFFSET(FAULT_ADDR, fault_addr);
DEFINE_OFFSET(FAULT_WAS_WRITE, fault_was_write);
