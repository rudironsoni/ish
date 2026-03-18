/*
 * aarch64 signal handling implementation
 */

#include "kernel/aarch64/signal.h"
#include "emu/aarch64/cpu.h"
#include "kernel/task.h"
#include "kernel/calls.h"
#include <string.h>

// From UTM's signal.c - Linux syscall number for rt_sigreturn on aarch64
#define A64_NR_rt_sigreturn 139

void a64_setup_sigcontext(struct a64_sigcontext *sc, struct cpu_state *cpu) {
    memset(sc, 0, sizeof(*sc));

    // Save fault address (for SIGSEGV)
    sc->fault_address = cpu->fault_addr;

    // Save general purpose registers x0-x30
    // Our x[0..30] map to x0..x30 in the signal context
    for (int i = 0; i < 31; i++) {
        sc->regs[i] = cpu->x[i];
    }

    // Save SP and PC
    sc->sp = cpu->sp;
    sc->pc = cpu->pc;

    // Save PSTATE
    sc->pstate = cpu->pstate;
}

void a64_setup_fpsimd_context(struct a64_fpsimd_context *fpsimd,
                               struct cpu_state *cpu) {
    memset(fpsimd, 0, sizeof(*fpsimd));

    fpsimd->head.magic = A64_FPSIMD_MAGIC;
    fpsimd->head.size = sizeof(*fpsimd);

    fpsimd->fpsr = cpu->fpsr;
    fpsimd->fpcr = cpu->fpcr;

    // Copy 128-bit vector registers
    // Each vreg is stored as low 64-bits, high 64-bits
    for (int i = 0; i < 32; i++) {
        __int128 v = cpu->v[i].q;
        fpsimd->vregs[i * 2] = (uint64_t)v;           // Low bits
        fpsimd->vregs[i * 2 + 1] = (uint64_t)(v >> 64); // High bits
    }
}

void a64_restore_sigcontext(struct cpu_state *cpu, struct a64_sigcontext *sc) {
    // Restore general purpose registers
    for (int i = 0; i < 31; i++) {
        cpu->x[i] = sc->regs[i];
    }

    // Restore SP and PC
    cpu->sp = sc->sp;
    cpu->pc = sc->pc;

    // Restore PSTATE
    cpu->pstate = sc->pstate;
}

void a64_restore_fpsimd_context(struct cpu_state *cpu,
                                 struct a64_fpsimd_context *fpsimd) {
    cpu->fpsr = fpsimd->fpsr;
    cpu->fpcr = fpsimd->fpcr;

    // Restore vector registers
    for (int i = 0; i < 32; i++) {
        uint64_t low = fpsimd->vregs[i * 2];
        uint64_t high = fpsimd->vregs[i * 2 + 1];
        cpu->v[i].q = ((__int128)high << 64) | low;
    }
}

/*
 * Calculate signal frame layout
 * Returns total size needed, including frame record
 */
static size_t a64_calc_sigframe_size(void) {
    size_t size = sizeof(struct a64_rt_sigframe);

    // Ensure we have at least the 4K reserved space
    if (size < sizeof(struct a64_rt_sigframe))
        size = sizeof(struct a64_rt_sigframe);

    // Add frame record for unwinding
    size += sizeof(struct a64_frame_record);

    // 16-byte alignment
    size = (size + 15) & ~15;

    return size;
}

/*
 * Get signal frame base address
 */
static uint64_t a64_get_sigframe_base(struct task *task,
                                       struct sigaction_ *action) {
    uint64_t sp;

    // Use alternate stack if set and SA_ONSTACK is set
    if (action->sa_flags & SA_ONSTACK_) {
        // TODO: Check if already on altstack
        sp = (uint64_t)task->sighand->altstack.ss_sp +
             task->sighand->altstack.ss_size;
    } else {
        sp = task->cpu.sp;
    }

    // Align to 16 bytes
    sp &= ~15;

    return sp;
}

int a64_setup_rt_frame(struct task *task, int sig, struct siginfo_ *info,
                       struct a64_rt_sigframe *frame) {
    struct cpu_state *cpu = &task->cpu;
    struct a64_sigcontext *sc = &frame->uc.uc_mcontext;
    struct a64_fpsimd_context *fpsimd;
    struct a64_context_header *end_ctx;

    // Setup ucontext header
    frame->uc.uc_flags = 0;
    frame->uc.uc_link = 0;
    frame->uc.uc_stack = task->sighand->altstack;

    // Setup main sigcontext
    a64_setup_sigcontext(sc, cpu);

    // FPSIMD context goes in the reserved area
    fpsimd = (struct a64_fpsimd_context *)sc->__reserved;
    a64_setup_fpsimd_context(fpsimd, cpu);

    // End marker after FPSIMD
    end_ctx = (struct a64_context_header *)((char *)fpsimd +
                                             sizeof(*fpsimd));
    end_ctx->magic = A64_END_MAGIC;
    end_ctx->size = 0;

    // Copy siginfo
    frame->info = *info;

    return 0;
}

/*
 * Setup signal delivery to a task
 */
void a64_deliver_signal(struct task *task, int sig, struct siginfo_ *info) {
    struct cpu_state *cpu = &task->cpu;
    struct sigaction_ *action = &task->sighand->action[sig];
    struct a64_rt_sigframe *frame;
    struct a64_frame_record *fr;
    uint64_t frame_addr, fr_addr, return_addr;
    size_t frame_size = a64_calc_sigframe_size();

    // Get frame location on guest stack
    frame_addr = a64_get_sigframe_base(task, action) - frame_size;

    // Map into host address space for setup
    // TODO: Use proper guest memory access
    frame = NULL; // Would use user_access helper here

    // Setup the frame
    a64_setup_rt_frame(task, sig, info, frame);

    // Calculate frame record location (at end of frame area)
    fr_addr = frame_addr + frame_size - sizeof(struct a64_frame_record);
    fr = (struct a64_frame_record *)((char *)frame +
                                      frame_size -
                                      sizeof(struct a64_frame_record));

    // Setup frame record for unwinding
    fr->fp = cpu->x[29];  // Save current FP
    fr->lr = cpu->x[30];  // Save current LR

    // Setup return address (sigtramp)
    if (action->sa_flags & SA_RESTORER_) {
        return_addr = (uint64_t)action->sa_restorer;
    } else {
        // Use default sigtramp from vdso
        return_addr = task->vdso_sigtramp;
    }

    // Modify CPU state for signal handler entry
    cpu->x[0] = sig;                          // First arg: signo
    cpu->x[1] = frame_addr +
                 offsetof(struct a64_rt_sigframe, info);  // Second arg: siginfo
    cpu->x[2] = frame_addr +
                 offsetof(struct a64_rt_sigframe, uc);    // Third arg: ucontext

    cpu->x[29] = fr_addr;                     // New frame pointer
    cpu->x[30] = return_addr;                 // Return to sigtramp
    cpu->sp = frame_addr;                     // New stack pointer
    cpu->pc = (uint64_t)action->handler;      // Jump to handler

    // Signal mask handling
    if (!(action->sa_flags & SA_NODEFER_)) {
        sigset_add(&task->blocked, sig);
    }
    if (action->sa_flags & SA_RESETHAND_) {
        action->handler = SIG_DFL_;
    }
}

/*
 * Setup sigtramp code
 * This is placed in the vdso or a dedicated page
 */
int a64_setup_sigtramp(uint64_t *tramp) {
    // mov x8, #139  (A64_NR_rt_sigreturn)
    // svc #0
    tramp[0] = A64_RT_SIGRETURN_CODE0;
    tramp[1] = A64_RT_SIGRETURN_CODE1;
    return 0;
}

/*
 * Handle sigreturn syscall
 * Restores CPU state from signal frame
 */
int a64_handle_sigreturn(struct cpu_state *cpu) {
    struct a64_rt_sigframe *frame;
    uint64_t frame_addr = cpu->sp;

    // Frame must be 16-byte aligned
    if (frame_addr & 15)
        return -1;

    // Map frame address
    frame = NULL; // Would use user_access helper here

    // Restore signal mask
    // TODO: Restore from frame->uc.uc_sigmask

    // Restore FPSIMD context
    struct a64_fpsimd_context *fpsimd =
        (struct a64_fpsimd_context *)frame->uc.uc_mcontext.__reserved;
    if (fpsimd->head.magic == A64_FPSIMD_MAGIC) {
        a64_restore_fpsimd_context(cpu, fpsimd);
    }

    // Restore main context
    a64_restore_sigcontext(cpu, &frame->uc.uc_mcontext);

    return 0;
}

/*
 * Handle rt_sigreturn syscall entry point
 */
dword_t sys_rt_sigreturn_aarch64(void) {
    struct task *task = current;
    struct cpu_state *cpu = &task->cpu;

    if (a64_handle_sigreturn(cpu) < 0) {
        // Force SIGSEGV on bad frame
        // TODO: deliver_signal(task, SIGSEGV_);
        return -_EFAULT;
    }

    // Success - don't change x0, just return to restored PC
    return 0;
}
