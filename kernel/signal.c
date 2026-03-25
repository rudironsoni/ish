/*
 * kernel/signal.c - Minimal signal handling implementation
 *
 * Provides basic signal delivery and sighand management.
 * This is a minimal implementation for AArch64 bring-up.
 */

#include "kernel/signal.h"
#include "kernel/task.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/aarch64/signal.h"
#include <stdlib.h>
#include <string.h>

/*
 * Create a new sighand structure
 */
struct sighand *sighand_new(void) {
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
struct sighand *sighand_copy(struct sighand *sighand) {
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
void sighand_release(struct sighand *sighand) {
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
void send_signal(struct task *task, int sig, struct siginfo_ info) {
    if (task == NULL || sig < 1 || sig >= NUM_SIGS) {
        return;
    }
    
    // Add signal to pending set
    task->pending |= sig_mask(sig);
}

/*
 * Deliver a signal to a task (ignore blocked/ignored status)
 */
void deliver_signal(struct task *task, int sig, struct siginfo_ info) {
    if (task == NULL || sig < 1 || sig >= NUM_SIGS) {
        return;
    }
    
    // Add signal to pending set
    task->pending |= sig_mask(sig);
}

/*
 * Try to send a signal to current if not blocked or ignored
 */
bool try_self_signal(int sig) {
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
    send_signal(task, sig, (struct siginfo_) {0});
    return true;
}

/*
 * Send a signal to all processes in a process group
 */
int send_group_signal(dword_t pgid, int sig, struct siginfo_ info) {
    // Minimal implementation - just return success for now
    // Real implementation would iterate through process group
    return 0;
}

/*
 * Receive and deliver pending signals
 * 
 * For AArch64 bring-up: handles fatal default signals by terminating
 * the task, and invokes architecture-specific delivery for handlers.
 */
void receive_signals(void) {
    struct task *task = current;
    if (task == NULL) {
        printk("[SIGNAL] receive_signals: task is NULL\n");
        return;
    }
    
    printk("[SIGNAL] receive_signals called, pending=0x%llx\n", (unsigned long long)task->pending);
    
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
            printk("[SIGNAL] No more pending signals found\n");
            break;  // No more pending signals
        }
        
        printk("[SIGNAL] Processing signal %d\n", sig);
        
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
                case SIGILL_:
                case SIGBUS_:
                case SIGFPE_:
                case SIGABRT_:
                case SIGTRAP_:
                case SIGSYS_:
                    // Fatal signal with default disposition - terminate the task
                    printk("[SIGNAL] Fatal signal %d with default disposition - calling do_exit_group\n", sig);
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
void sigmask_set_temp(sigset_t_ mask) {
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
dword_t sys_kill(pid_t_ pid, dword_t sig) {
    // Minimal implementation - just return success
    // Real implementation would look up process and send signal
    if (sig == 0) {
        return 0; // Signal 0 is error check only
    }
    return 0;
}

// sys_tkill - Send signal to thread
dword_t sys_tkill(pid_t_ tid, dword_t sig) {
    // Minimal implementation
    if (sig == 0) {
        return 0;
    }
    return 0;
}

// sys_tgkill - Send signal to thread group
dword_t sys_tgkill(pid_t_ tgid, pid_t_ tid, dword_t sig) {
    // Minimal implementation
    if (sig == 0) {
        return 0;
    }
    return 0;
}

// sys_rt_sigaction - Examine and change signal action
dword_t sys_rt_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr, dword_t sigset_size) {
    // Minimal implementation - just return success
    (void)signum;
    (void)action_addr;
    (void)oldaction_addr;
    (void)sigset_size;
    return 0;
}

// sys_rt_sigprocmask - Examine and change blocked signals
dword_t sys_rt_sigprocmask(dword_t how, addr_t set, addr_t oldset, dword_t size) {
    // Minimal implementation
    (void)how;
    (void)set;
    (void)oldset;
    (void)size;
    return 0;
}

// sys_rt_sigreturn - Return from signal handler
dword_t sys_rt_sigreturn(void) {
    // Should be handled by signal frame setup
    // This stub should not be reached in normal execution
    return -_EINVAL;
}

// sys_rt_sigsuspend - Wait for signal
int_t sys_rt_sigsuspend(addr_t mask_addr, uint_t size) {
    (void)mask_addr;
    (void)size;
    // Minimal implementation - just return success
    return 0;
}

// sys_sigaltstack - Set/get signal stack context
dword_t sys_sigaltstack(addr_t ss, addr_t old_ss) {
    (void)ss;
    (void)old_ss;
    return 0;
}
