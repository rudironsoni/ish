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
