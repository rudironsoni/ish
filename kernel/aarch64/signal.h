#ifndef AARCH64_SIGNAL_H
#define AARCH64_SIGNAL_H

/*
 * aarch64 signal handling structures
 * Matches Linux kernel arch/arm64/include/uapi/asm/sigcontext.h
 */

#include "misc.h"
#include "kernel/signal.h"

// Forward declaration
struct cpu_state;

// Size of reserved area in sigcontext
#define A64_SIGCONTEXT_RESERVED_SIZE 4096

// FPSIMD context magic
#define A64_FPSIMD_MAGIC 0x46508001
#define A64_FPSIMD_CONTEXT_SIZE sizeof(struct a64_fpsimd_context)

// Extra context magic
#define A64_EXTRA_MAGIC 0x45585401

// End marker
#define A64_END_MAGIC 0

// SVE magic (if needed)
#define A64_SVE_MAGIC 0x53564501

/*
 * sigcontext - saved signal state
 * This is what the signal handler receives in its third argument
 */
struct a64_sigcontext {
    uint64_t fault_address;       // Faulting address (for SIGSEGV)
    uint64_t regs[31];            // x0-x30 (x30 is lr)
    uint64_t sp;                  // Stack pointer
    uint64_t pc;                  // Program counter
    uint64_t pstate;              // PSTATE (flags)
    uint8_t __reserved[A64_SIGCONTEXT_RESERVED_SIZE] __attribute__((aligned(16)));
};

/*
 * Header for extended context records
 */
struct a64_context_header {
    uint32_t magic;
    uint32_t size;
};

/*
 * FPSIMD context - floating point and SIMD state
 */
struct a64_fpsimd_context {
    struct a64_context_header head;
    uint32_t fpsr;                // FP status register
    uint32_t fpcr;                // FP control register
    // vregs are 128-bit, stored as pairs of uint64_t
    // Layout: v0[low,high], v1[low,high], ..., v31[low,high]
    uint64_t vregs[32 * 2];
};

/*
 * Extra context - points to additional context data
 */
struct a64_extra_context {
    struct a64_context_header head;
    uint64_t datap;               // Pointer to extra data (16-byte aligned)
    uint32_t size;                // Size of extra data
    uint32_t reserved[3];
};

/*
 * ucontext - user context for signal handlers
 */
struct a64_ucontext {
    uint64_t uc_flags;
    uint64_t uc_link;             // struct a64_ucontext *
    struct stack_t_ uc_stack;     // Signal stack (from kernel/signal.h)
    sigset_t_ uc_sigmask;         // Current signal mask (kernel/signal.h)
    // Padding for sigset_t size (1024 bits = 128 bytes)
    uint8_t __sigset_padding[128 - sizeof(sigset_t_)];
    // Must be last for future expansion
    struct a64_sigcontext uc_mcontext;
};

/*
 * RT signal frame layout on stack
 */
struct a64_rt_sigframe {
    struct siginfo_ info;         // Signal info (from kernel/signal.h)
    struct a64_ucontext uc;
};

/*
 * Frame record for unwinding
 * Placed after the sigframe on the stack
 */
struct a64_frame_record {
    uint64_t fp;                  // Frame pointer (x29)
    uint64_t lr;                  // Link register (x30)
};

// Sigreturn trampoline code (placed in vdso or created per-process)
// mov x8, #__NR_rt_sigreturn (139)
// svc #0
#define A64_RT_SIGRETURN_CODE0 0xd2801168  // mov x8, #139
#define A64_RT_SIGRETURN_CODE1 0xd4000001  // svc #0

// Setup functions
void a64_setup_sigcontext(struct a64_sigcontext *sc, struct cpu_state *cpu);
void a64_setup_fpsimd_context(struct a64_fpsimd_context *fpsimd, struct cpu_state *cpu);
void a64_restore_sigcontext(struct cpu_state *cpu, struct a64_sigcontext *sc);
void a64_restore_fpsimd_context(struct cpu_state *cpu, struct a64_fpsimd_context *fpsimd);

// Frame setup
int a64_setup_rt_frame(struct task *task, int sig, struct siginfo_ *info,
                       struct a64_rt_sigframe *frame);
int a64_setup_sigtramp(uint64_t *tramp);

// Signal delivery
void a64_deliver_signal(struct task *task, int sig, struct siginfo_ *info);
int a64_handle_sigreturn(struct cpu_state *cpu);

#endif
