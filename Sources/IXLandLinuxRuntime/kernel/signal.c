/*
 * kernel/signal.c - Minimal signal handling implementation
 *
 * Provides basic signal delivery and sighand management.
 * This is a minimal implementation for AArch64 bring-up.
 */

#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/interrupt.h>
#import <IXLandLinuxRuntime/kernel/aarch64/signal.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/signal.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct guest_kernel_sigaction {
    addr_t handler;
    uint64_t flags;
    addr_t restorer;
    sigset_t_ mask;
} __attribute__((packed));

static_assert(sizeof(struct guest_kernel_sigaction) == 32,
              "AArch64 kernel rt_sigaction ABI must stay 32 bytes");

static bool signal_number_valid(int sig)
{
    return sig > 0 && sig < NUM_SIGS;
}

static bool signal_action_mutable(int sig)
{
    return sig != SIGKILL_ && sig != SIGSTOP_;
}

static struct guest_kernel_sigaction signal_action_to_guest(struct sigaction_ action)
{
    return (struct guest_kernel_sigaction){
        .handler = action.handler,
        .flags = action.flags,
        .restorer = action.restorer,
        .mask = action.mask,
    };
}

static struct sigaction_ signal_action_from_guest(struct guest_kernel_sigaction action)
{
    return (struct sigaction_){
        .handler = action.handler,
        .flags = action.flags,
        .restorer = action.restorer,
        .mask = action.mask,
    };
}

/*
 * Create a new sighand structure
 */
struct sighand *sighand_new(void)
{
    struct sighand *sighand = malloc(sizeof(struct sighand));
    if (sighand == NULL) {
        return NULL;
    }

    memset(sighand, 0, sizeof(*sighand));
    sighand->refcount = 1;
    lock_init(&sighand->lock);

    // Initialize default signal actions (SIG_DFL for all)
    for (int i = 0; i < NUM_SIGS; i++) {
        sighand->action[i].handler = SIG_DFL_;
    }

    return sighand;
}

/*
 * Copy a sighand structure (for fork)
 */
struct sighand *sighand_copy(struct sighand *sighand)
{
    if (sighand == NULL) {
        return sighand_new();
    }

    // Increment refcount for shared sighand
    sighand->refcount++;
    return sighand;
}

/*
 * Release a sighand structure
 */
void sighand_release(struct sighand *sighand)
{
    if (sighand == NULL) {
        return;
    }

    if (--sighand->refcount == 0) {
        // Last reference - free the structure
        free(sighand);
    }
}

/*
 * Send a signal to a task (checks blocked/ignored)
 */
void send_signal(struct task *task, int sig, struct siginfo_ info)
{
    (void)info;
    if (task == NULL || sig < 1 || sig >= NUM_SIGS) {
        return;
    }

    // Add signal to pending set
    task->pending |= sig_mask(sig);
}

/*
 * Deliver a signal to a task (ignore blocked/ignored status)
 */
void deliver_signal(struct task *task, int sig, struct siginfo_ info)
{
    (void)info;
    if (task == NULL || sig < 1 || sig >= NUM_SIGS) {
        return;
    }

    // Add signal to pending set
    task->pending |= sig_mask(sig);
}

/*
 * Try to send a signal to current if not blocked or ignored
 */
bool try_self_signal(int sig)
{
    if (sig < 1 || sig >= NUM_SIGS) {
        return false;
    }

    struct task *task = current;
    if (task == NULL) {
        return false;
    }

    // Check if signal is blocked
    if (task->blocked & sig_mask(sig)) {
        return false;
    }

    // Check if signal is ignored
    if (task->sighand->action[sig].handler == SIG_IGN_) {
        return false;
    }

    // Send the signal
    send_signal(task, sig, (struct siginfo_){ 0 });
    return true;
}

/*
 * Send a signal to all processes in a process group
 */
int send_group_signal(uint32_t pgid, int sig, struct siginfo_ info)
{
    (void)pgid;
    (void)sig;
    (void)info;
    // Minimal implementation - just return success for now
    // Real implementation would iterate through process group
    return 0;
}

static void trace_receive_signals_checkpoint(const char *name, int sig, uint64_t pending_mask)
{
    if (trace_get_level() < TRACE_LEVEL_DEBUG)
        return;

    struct cpu_state *cpu = current ? &current->cpu : NULL;
    char task_buf[32];
    char pid_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char pending_mask_buf[32];
    char sig_buf[32];
    char signal_code_buf[32];
    char fault_addr_buf[32];
    char fault_write_buf[32];
    char pc_buf[32];
    char interrupt_buf[32];

    snprintf(task_buf, sizeof(task_buf), "%p", (void *)current);
    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(current ? current->pid : 0));
    snprintf(mm_buf, sizeof(mm_buf), "%p", current ? (void *)current->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", current ? (void *)current->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", cpu ? (void *)cpu->mmu : NULL);
    snprintf(pending_mask_buf, sizeof(pending_mask_buf), "0x%llx",
             (unsigned long long)pending_mask);
    snprintf(sig_buf, sizeof(sig_buf), "%d", sig);
    snprintf(signal_code_buf, sizeof(signal_code_buf), "%d", 0);
    snprintf(fault_addr_buf, sizeof(fault_addr_buf), "0x%llx",
             cpu ? (unsigned long long)cpu->fault_addr : 0ULL);
    snprintf(fault_write_buf, sizeof(fault_write_buf), "%d", cpu ? cpu->fault_was_write : 0);
    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", cpu ? (unsigned long long)cpu->pc : 0ULL);
    snprintf(interrupt_buf, sizeof(interrupt_buf), "%d", INT_GPF);

    trace_attribute_t attrs[] = {
        { "task", task_buf },
        { "pid", pid_buf },
        { "mm", mm_buf },
        { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf },
        { "pending_mask", pending_mask_buf },
        { "selected_sig", sig_buf },
        { "signal_code", signal_code_buf },
        { "fault_addr", fault_addr_buf },
        { "fault_write", fault_write_buf },
        { "guest_pc", pc_buf },
        { "interrupt", interrupt_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

/*
 * Receive and deliver pending signals
 *
 * For AArch64 bring-up: handles fatal default signals by terminating
 * the task, and invokes architecture-specific delivery for handlers.
 */
void receive_signals(void)
{
    struct task *task = current;
    if (task == NULL) {
        return;
    }
    if (task->pending == 0) {
        return;
    }

    trace_receive_signals_checkpoint("task.proof.receive_signals.entry", 0, task->pending);
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.receive_signals.entry");

    trace_receive_signals_checkpoint("task.proof.receive_signals.pending_mask_snapshot", 0,
                                     task->pending);

    // Process each pending signal
    while (task->pending != 0) {
        // Find the first pending signal
        int sig = 0;
        for (int i = 1; i < NUM_SIGS; i++) {
            if (task->pending & sig_mask(i)) {
                sig = i;
                break;
            }
        }

        if (sig == 0) {
            break; // No more pending signals
        }

        trace_receive_signals_checkpoint("task.proof.receive_signals.selected_signal", sig,
                                         task->pending);
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                    "guest.receive_signals.selected", "selected_sig", sig);

        {
            char sig_buf[16];
            char pending_buf[32];
            snprintf(sig_buf, sizeof(sig_buf), "%d", sig);
            snprintf(pending_buf, sizeof(pending_buf), "0x%llx", (unsigned long long)task->pending);
            ixland_instrumentation_attribute_t attrs[] = {
                { .key = "signal", .value = sig_buf },
                { .key = "pending_mask", .value = pending_buf },
            };
            ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                          "guest.signal.handle", attrs,
                                          sizeof(attrs) / sizeof(attrs[0]));
        }

        // Clear this signal from pending set
        sigset_del(&task->pending, sig);

        // Skip if signal is blocked
        if (task->blocked & sig_mask(sig)) {
            continue;
        }

        // Check signal disposition
        struct sigaction_ *action = &task->sighand->action[sig];

        if (action->handler == SIG_DFL_) {
            // Default disposition - handle fatal signals
            switch (sig) {
            case SIGSEGV_:
                trace_receive_signals_checkpoint("task.proof.receive_signals.before_sigsegv_path",
                                                 sig, task->pending);
                [[fallthrough]];
            case SIGILL_:
            case SIGBUS_:
            case SIGFPE_:
            case SIGABRT_:
            case SIGTRAP_:
            case SIGSYS_:
                // Fatal signal with default disposition - terminate the task
                trace_receive_signals_checkpoint("task.proof.receive_signals.before_fatal_exit",
                                                 sig, task->pending);
                // Use exit code 128 + signal number (standard Unix convention)
                do_exit_group(128 + sig);
                // do_exit_group does not return
                break;

            case SIGCHLD_:
            case SIGURG_:
            case SIGWINCH_:
                // Ignore these signals by default
                break;

            default:
                // All other signals: terminate
                do_exit_group(128 + sig);
                // do_exit_group does not return
                break;
            }
        } else if (action->handler == SIG_IGN_) {
            // Signal is ignored - do nothing
            continue;
        } else {
            // Custom signal handler - deliver via architecture-specific path
            // For AArch64, set up the signal frame
            struct siginfo_ info = {
                .sig = sig,
                .code = SI_USER_,
            };
            a64_deliver_signal(task, sig, &info);
            // Signal delivered - only one signal per invocation
            break;
        }
    }
}

/*
 * Set temporary signal mask
 */
void sigmask_set_temp(sigset_t_ mask)
{
    struct task *task = current;
    if (task == NULL) {
        return;
    }

    task->blocked = mask;
}

/*
 * Syscall implementations for signal handling
 */

// sys_kill - Send signal to process
int32_t sys_kill(pid_t_ pid, int32_t sig)
{
    (void)pid;
    // Minimal implementation - just return success
    // Real implementation would look up process and send signal
    if (sig == 0) {
        return 0; // Signal 0 is error check only
    }
    return 0;
}

// sys_tkill - Send signal to thread
int32_t sys_tkill(pid_t_ tid, int32_t sig)
{
    (void)tid;
    // Minimal implementation
    if (sig == 0) {
        return 0;
    }
    return 0;
}

// sys_tgkill - Send signal to thread group
int32_t sys_tgkill(pid_t_ tgid, pid_t_ tid, int32_t sig)
{
    (void)tgid;
    (void)tid;
    // Minimal implementation
    if (sig == 0) {
        return 0;
    }
    return 0;
}

// sys_rt_sigaction - Examine and change signal action
int32_t sys_rt_sigaction(int32_t signum, addr_t action_addr, addr_t oldaction_addr,
                         uint32_t sigset_size)
{
    if (current == NULL || current->sighand == NULL) {
        return _EINVAL;
    }
    if (!signal_number_valid(signum) || sigset_size != sizeof(sigset_t_)) {
        return _EINVAL;
    }
    if (action_addr != 0 && !signal_action_mutable(signum)) {
        return _EINVAL;
    }

    struct guest_kernel_sigaction guest_new_action = { 0 };
    if (action_addr != 0 && user_get(action_addr, guest_new_action)) {
        return _EFAULT;
    }

    lock(&current->sighand->lock);
    struct sigaction_ old_action = current->sighand->action[signum];
    if (action_addr != 0) {
        current->sighand->action[signum] = signal_action_from_guest(guest_new_action);
    }
    unlock(&current->sighand->lock);

    if (oldaction_addr != 0) {
        struct guest_kernel_sigaction guest_old_action = signal_action_to_guest(old_action);
        if (user_put(oldaction_addr, guest_old_action)) {
            return _EFAULT;
        }
    }

    return 0;
}

// sys_rt_sigprocmask - Examine and change blocked signals
int32_t sys_rt_sigprocmask(int32_t how, addr_t set, addr_t oldset, uint32_t size)
{
    if (current == NULL) {
        return _EINVAL;
    }
    if (size != sizeof(sigset_t_)) {
        return _EINVAL;
    }

    sigset_t_ old_mask = current->blocked;
    sigset_t_ new_mask = 0;

    if (set != 0 && user_get(set, new_mask)) {
        return _EFAULT;
    }

    if (oldset != 0 && user_put(oldset, old_mask)) {
        return _EFAULT;
    }

    if (set != 0) {
        sigset_t_ blocked = current->blocked;
        sigset_t_ unmaskable = sig_mask(SIGKILL_) | sig_mask(SIGSTOP_);
        new_mask &= ~unmaskable;

        switch (how) {
        case SIG_BLOCK_:
            blocked |= new_mask;
            break;
        case SIG_UNBLOCK_:
            blocked &= ~new_mask;
            break;
        case SIG_SETMASK_:
            blocked = new_mask;
            break;
        default:
            return _EINVAL;
        }

        current->blocked = blocked;
    }

    return 0;
}

// sys_rt_sigreturn - Return from signal handler
int32_t sys_rt_sigreturn(void)
{
    // Should be handled by signal frame setup
    // This stub should not be reached in normal execution
    return -_EINVAL;
}

// sys_rt_sigsuspend - Wait for signal
int64_t sys_rt_sigsuspend(addr_t mask_addr, uint32_t size)
{
    (void)mask_addr;
    (void)size;
    // Minimal implementation - just return success
    return 0;
}

// sys_sigaltstack - Set/get signal stack context
int32_t sys_sigaltstack(addr_t ss, addr_t old_ss)
{
    (void)ss;
    (void)old_ss;
    return 0;
}

// sys_rt_sigpending - Examine pending signals
int64_t sys_rt_sigpending(addr_t set_addr)
{
    (void)set_addr;
    return _ENOSYS;
}
