#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/ptrace.h>
#import <IXLandLinuxRuntime/kernel/signal.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#include <string.h>

// Returns stopped child with the given pid, locked with the ptrace lock
static struct task *find_child(pid_t_ pid)
{
    struct task *child = NULL;
    list_for_each_entry (&current->children, child, siblings) {
        if (child->pid == pid) {
            lock(&child->ptrace.lock);
            if (child->ptrace.stopped) {
                goto found;
            }

            unlock(&child->ptrace.lock);
        }
    }
    child = NULL;
found:
    return child;
}

// Ensure stopped, ptrace locked, etc. before calling this
static void get_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *user_regs_)
{
    // Map aarch64 x[0-5] to x86 ebx,ecx,edx,esi,edi for compatibility
    user_regs_->ebx = cpu->x[0];
    user_regs_->ecx = cpu->x[1];
    user_regs_->edx = cpu->x[2];
    user_regs_->esi = cpu->x[3];
    user_regs_->edi = cpu->x[4];
    user_regs_->ebp = cpu->x[5];
    user_regs_->eax = cpu->x[6];
    user_regs_->orig_eax = cpu->x[6];
    user_regs_->eip = cpu->pc;
    user_regs_->eflags = 0; // aarch64 doesn't have eflags
    user_regs_->esp = cpu->sp;
}

// Ensure stopped, ptrace locked, etc. before calling this
static void set_user_regs(struct cpu_state *cpu, struct user_regs_struct_ *user_regs_)
{
    // Map x86 user_regs back to aarch64 x[0-5]
    cpu->x[0] = user_regs_->ebx;
    cpu->x[1] = user_regs_->ecx;
    cpu->x[2] = user_regs_->edx;
    cpu->x[3] = user_regs_->esi;
    cpu->x[4] = user_regs_->edi;
    cpu->x[5] = user_regs_->ebp;
    cpu->x[6] = user_regs_->eax;
    cpu->pc = user_regs_->eip;
    // cpu->eflags not applicable to aarch64
    cpu->sp = user_regs_->esp;
}

int32_t sys_ptrace(int32_t request, int32_t pid, addr_t addr, int32_t data)
{
    switch (request) {
    case PTRACE_TRACEME_:
        STRACE("ptrace(PTRACE_TRACEME, %d, %#x, %#x)", pid, addr, data);
        current->ptrace.traced = true;
        return 0;

    case PTRACE_PEEKTEXT_:
    case PTRACE_PEEKDATA_: {
        STRACE("ptrace(PTRACE_PEEKDATA, %d, %#x, %#x)", pid, addr, data);
        uint32_t peek;
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        if (user_get_task(child, addr, peek)) {
            unlock(&child->ptrace.lock);
            return _EFAULT;
        } else if (user_put(data, peek)) {
            unlock(&child->ptrace.lock);
            return _EFAULT;
        }
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_PEEKUSER_: {
        STRACE("ptrace(PTRACE_PEEKUSER, %d, %#x, %#x)", pid, addr, data);
        dword_t peek;
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        struct user_ user_ = {};
        get_user_regs(&child->cpu, &user_.user_regs);

        if (addr & (sizeof(peek) - 1) || addr >= sizeof(struct user_))
            return _EIO;

        memcpy(&peek, (char *)&user_ + addr, sizeof(peek));
        if (user_put(data, peek)) {
            unlock(&child->ptrace.lock);
            return _EFAULT;
        }
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_POKETEXT_:
    case PTRACE_POKEDATA_: {
        STRACE("ptrace(PTRACE_POKEDATA, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        if (user_write_task_ptrace(child, addr, &data, sizeof(data))) {
            unlock(&child->ptrace.lock);
            return _EFAULT;
        }
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_CONT_: {
        STRACE("ptrace(PTRACE_CONT, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        // aarch64 doesn't have a trap flag like x86
        // single-stepping is done via different mechanism
        child->ptrace.stopped = false;
        notify(&child->ptrace.cond);
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_KILL_: {
        STRACE("ptrace(PTRACE_KILL, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        child->ptrace.stopped = false;
        send_signal(child, SIGKILL_, SIGINFO_NIL);
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_SINGLESTEP_: {
        STRACE("ptrace(PTRACE_SINGLESTEP, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        // aarch64 doesn't have a trap flag like x86
        // single-stepping is done via different mechanism
        child->ptrace.stopped = false;
        notify(&child->ptrace.cond);
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_GETREGS_: {
        STRACE("ptrace(PTRACE_GETREGS, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        struct user_regs_struct_ user_regs_ = {};
        get_user_regs(&child->cpu, &user_regs_);
        if (user_put(data, user_regs_)) {
            unlock(&child->ptrace.lock);
            return _EFAULT;
        }
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_SETREGS_: {
        STRACE("ptrace(PTRACE_SETREGS, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        struct user_regs_struct_ user_regs_;
        if (user_get(data, user_regs_)) {
            return _EFAULT;
        } else {
            set_user_regs(&child->cpu, &user_regs_);
        }
        unlock(&child->ptrace.lock);

        return 0;
    }

    // GDB needs the fpregs functions to exist if you want to evaluate things
    case PTRACE_GETFPREGS_: {
        STRACE("ptrace(PTRACE_GETFPREGS, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        struct user_fpregs_struct_ user_fpregs_ = {};
        if (user_put(data, user_fpregs_)) {
            unlock(&child->ptrace.lock);
            return _EFAULT;
        }
        // TODO get float point registers
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_SETFPREGS_: {
        STRACE("ptrace(PTRACE_SETFPREGS, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        struct user_fpregs_struct_ user_fpregs_;
        if (user_get(data, user_fpregs_)) {
            return _EFAULT;
        } else {
            // TODO set floating point registers
        }
        unlock(&child->ptrace.lock);

        return 0;
    }

    case PTRACE_SETOPTIONS_:
        STRACE("ptrace(PTRACE_SETOPTIONS, %d, %#x, %#x)", pid, addr, data);
        return _EINVAL;

    case PTRACE_GETSIGINFO_: {
        STRACE("ptrace(PTRACE_GETSIGINFO, %d, %#x, %#x)", pid, addr, data);
        struct task *child = find_child(pid);
        if (!child)
            return _EPERM;

        if (data && user_put(data, child->ptrace.info)) {
            return _EFAULT;
        }
        unlock(&child->ptrace.lock);

        return 0;
    }

    default:
        STRACE("ptrace(%d, %d, %#x, %#x)", request, pid, addr, data);
        return _EPERM;
    }
}
