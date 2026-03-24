/*
 * kernel/signal.c - Minimal signal handling implementation
 *
 * Provides basic signal delivery and sighand management.
 * This is a minimal implementation for AArch64 bring-up.
 */

#include "kernel/signal.h"
#include "kernel/task.h"
#include "kernel/errno.h"
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
 */
void receive_signals(void) {
    struct task *task = current;
    if (task == NULL) {
        return;
    }
    
    // Minimal implementation - just clear pending for now
    // Real implementation would invoke signal handlers
    task->pending = 0;
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
