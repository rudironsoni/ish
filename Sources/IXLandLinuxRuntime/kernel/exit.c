#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/interrupt.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/futex.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/mm.h>
#import <IXLandLinuxRuntime/kernel/ptrace.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void halt_system(void);

static bool exit_tgroup(struct task *task)
{
    struct tgroup *group = task->group;
    list_remove(&task->group_links);
    bool group_dead = list_empty(&group->threads);
    if (group_dead) {
        // don't need to lock the group since the only pointers to it come from:
        // - other threads' current->group, but there are none left thanks to that list_empty call
        // - locking pids_lock first, which do_exit did
        if (group->itimer)
            timer_free(group->itimer);

        // The group will be removed from its group and session by reap_if_zombie,
        // because fish tries to set the pgid to that of an exited but not reaped
        // task.
        // https://github.com/Microsoft/WSL/issues/2786
    }
    return group_dead;
}

void (*exit_hook)(struct task *task, int code) = NULL;

// When running under GCD (e.g., in test harness), pthread_exit is unsafe
// because it conflicts with libdispatch's thread lifecycle management.
// Set this to false to skip pthread_exit and return normally.
bool exit_should_pthread_exit = true;

static struct task *find_new_parent(struct task *task)
{
    struct task *new_parent;
    list_for_each_entry (&task->group->threads, new_parent, group_links) {
        if (!new_parent->exiting)
            return new_parent;
    }
    return pid_get_task(1);
}

void do_exit(int status)
{
    {
        char status_buf[32];
        char code_buf[32];
        snprintf(status_buf, sizeof(status_buf), "%d", status);
        snprintf(code_buf, sizeof(code_buf), "%d", status >> 8);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "status", .value = status_buf },
            { .key = "code", .value = code_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                      "session.process.exit", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
    }

    {
        char status_buf[32];
        char pid_buf[32];
        char leader_pid_buf[32];
        char is_leader_buf[8];
        char parent_pid_buf[32];
        snprintf(status_buf, sizeof(status_buf), "%d", status);
        snprintf(pid_buf, sizeof(pid_buf), "%d", current ? current->pid : -1);
        int leader_pid = (current && current->group && current->group->leader) ? current->group->leader->pid : -1;
        int parent_pid = (current && current->parent) ? current->parent->pid : -1;
        snprintf(leader_pid_buf, sizeof(leader_pid_buf), "%d", leader_pid);
        snprintf(parent_pid_buf, sizeof(parent_pid_buf), "%d", parent_pid);
        snprintf(is_leader_buf, sizeof(is_leader_buf), "%d",
                 (current && current->group && current->group->leader == current) ? 1 : 0);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "status", .value = status_buf },
            { .key = "pid", .value = pid_buf },
            { .key = "leader_pid", .value = leader_pid_buf },
            { .key = "parent_pid", .value = parent_pid_buf },
            { .key = "is_leader", .value = is_leader_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                      "guest.do_exit.entry", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
    }

    // has to happen before mm_release
    addr_t clear_tid = current->clear_tid;
    if (clear_tid) {
        pid_t_ zero = 0;
        if (user_put(clear_tid, zero) == 0)
            futex_wake(clear_tid, 1);
    }

    // release all our resources
    struct mm *mm = current->mm;
    current->mm = NULL;
    current->mem = NULL;
    current->cpu.mmu = NULL;
    mm_release(mm);
    fdtable_release(current->files);
    current->files = NULL;
    fs_info_release(current->fs);
    current->fs = NULL;
    // sighand must be released below so it can be protected by pids_lock
    // since it can be accessed by other threads

    // save things that our parent might be interested in
    current->exit_code = status; // FIXME locking
    struct rusage_ rusage = rusage_get_current();
    lock(&current->group->lock);
    rusage_add(&current->group->rusage, &rusage);
    struct rusage_ group_rusage = current->group->rusage;
    unlock(&current->group->lock);

    // the actual freeing needs pids_lock
    lock(&pids_lock);
    current->exiting = true;
    // release the sighand
    sighand_release(current->sighand);
    current->sighand = NULL;
    struct sigqueue *sigqueue, *sigqueue_tmp;
    list_for_each_entry_safe(&current->queue, sigqueue, sigqueue_tmp, queue)
    {
        list_remove(&sigqueue->queue);
        free(sigqueue);
    }
    struct task *leader = current->group->leader;

    // reparent children
    struct task *new_parent = find_new_parent(current);
    struct task *child, *tmp;
    list_for_each_entry_safe(&current->children, child, tmp, siblings)
    {
        child->parent = new_parent;
        list_remove(&child->siblings);
        list_add(&new_parent->children, &child->siblings);
    }

    if (exit_tgroup(current)) {
        // notify parent that we died
        struct task *parent = leader->parent;
        if (parent == NULL) {
            // init died
            halt_system();
        } else {
            leader->zombie = true;
            notify(&parent->group->child_exit);
            struct siginfo_ info = {
                .code = SI_KERNEL_,
                .child.pid = current->pid,
                .child.uid = current->uid,
                .child.status = current->exit_code,
                .child.utime = clock_from_timeval(group_rusage.utime),
                .child.stime = clock_from_timeval(group_rusage.stime),
            };
            if (leader->exit_signal != 0)
                send_signal(parent, leader->exit_signal, info);
        }

        if (exit_hook != NULL)
            exit_hook(current, status);
    }

    vfork_notify(current);
    if (current != leader)
        task_destroy(current);
    unlock(&pids_lock);

    if (exit_should_pthread_exit) {
        pthread_exit(NULL);
    }
    // Return normally when running under GCD (pthread_exit would crash libdispatch)
}

static void trace_do_exit_group_checkpoint(const char *name, int status)
{
    struct cpu_state *cpu = current ? &current->cpu : NULL;
    char task_buf[32];
    char pid_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char status_buf[32];
    char fault_addr_buf[32];
    char fault_write_buf[32];
    char pc_buf[32];

    snprintf(task_buf, sizeof(task_buf), "%p", (void *)current);
    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(current ? current->pid : 0));
    snprintf(mm_buf, sizeof(mm_buf), "%p", current ? (void *)current->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", current ? (void *)current->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", cpu ? (void *)cpu->mmu : NULL);
    snprintf(status_buf, sizeof(status_buf), "%d", status);
    snprintf(fault_addr_buf, sizeof(fault_addr_buf), "0x%llx",
             cpu ? (unsigned long long)cpu->fault_addr : 0ULL);
    snprintf(fault_write_buf, sizeof(fault_write_buf), "%d", cpu ? cpu->fault_was_write : 0);
    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", cpu ? (unsigned long long)cpu->pc : 0ULL);

    trace_attribute_t attrs[] = {
        { "task", task_buf },
        { "pid", pid_buf },
        { "mm", mm_buf },
        { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf },
        { "status", status_buf },
        { "fault_addr", fault_addr_buf },
        { "fault_write", fault_write_buf },
        { "guest_pc", pc_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

void do_exit_group(int status)
{
    trace_do_exit_group_checkpoint("task.proof.do_exit_group.entry", status);
    ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.do_exit_group.entry",
                                "status", status);
    struct tgroup *group = current->group;
    lock(&pids_lock);
    lock(&group->lock);
    if (!group->doing_group_exit) {
        group->doing_group_exit = true;
        group->group_exit_code = status;
    } else {
        status = group->group_exit_code;
    }

    trace_do_exit_group_checkpoint("task.proof.do_exit_group.before_kill_others", status);

    // kill everyone else in the group
    struct task *task;
    list_for_each_entry (&group->threads, task, group_links) {
        deliver_signal(task, SIGKILL_, SIGINFO_NIL);
        task->group->stopped = false;
        notify(&task->group->stopped_cond);
    }

    trace_do_exit_group_checkpoint("task.proof.do_exit_group.before_do_exit", status);

    unlock(&group->lock);
    unlock(&pids_lock);
    do_exit(status);
}

// always called from init process
static void halt_system(void)
{
    for (int state = 0; state < 3; state++) {
        int tasks_found = 0;
        for (int i = 2; i < MAX_PID; i++) {
            struct task *task = pid_get_task(i);
            if (task != NULL) {
                tasks_found++;
                switch (state) {
                case 0:
                    deliver_signal(task, SIGTERM_, SIGINFO_NIL);
                    break;
                case 1:
                    deliver_signal(task, SIGKILL_, SIGINFO_NIL);
                    break;
                case 2:
                    pthread_kill(task->thread, SIGTERM);
                }
            }
        }
        if (tasks_found == 0)
            break;
        if (state != 2)
            sleep(1);
    }

    // unmount all filesystems
    lock(&mounts_lock);
    struct mount *mount, *tmp;
    list_for_each_entry_safe(&mounts, mount, tmp, mounts)
    {
        mount_remove(mount);
    }
    unlock(&mounts_lock);
}

uint32_t sys_exit(uint32_t status)
{
    STRACE("exit(%d)\n", status);
    do_exit(status << 8);
    return 0; // never reached, but compiler requires it
}

uint32_t sys_exit_group(uint32_t status)
{
    STRACE("exit_group(%d)\n", status);
    do_exit_group(status << 8);
    return 0; // never reached, but compiler requires it
}

#define WNOHANG_    (1 << 0)
#define WUNTRACED_  (1 << 1)
#define WEXITED_    (1 << 2)
#define WCONTINUED_ (1 << 3)
#define WNOWAIT_    (1 << 24)
#define __WALL_     (1 << 30)

#define P_ALL_  0
#define P_PID_  1
#define P_PGID_ 2

// returns false if the task cannot be reaped and true if the task was reaped
static bool reap_if_zombie(struct task *task, struct siginfo_ *info_out, struct rusage_ *rusage_out,
                           int options)
{
    if (!task->zombie)
        return false;
    lock(&task->group->lock);

    uint32_t exit_code = task->exit_code;
    if (task->group->doing_group_exit)
        exit_code = task->group->group_exit_code;
    info_out->child.status = exit_code;

    struct rusage_ rusage = task->group->rusage;
    if (!(options & WNOWAIT_)) {
        lock(&current->group->lock);
        rusage_add(&current->group->children_rusage, &rusage);
        current->group->last_reaped_pid = task->pid;
        current->group->last_reaped_status = info_out->child.status;
        current->group->last_reaped_valid = true;
        unlock(&current->group->lock);
    }
    if (rusage_out != NULL)
        *rusage_out = rusage;

    unlock(&task->group->lock);

    // WNOWAIT means don't destroy the child, instead leave it so it could be waited for again.
    if (options & WNOWAIT_)
        return true;

    {
        char waiter_pid_buf[32];
        char reaped_pid_buf[32];
        char options_buf[32];
        snprintf(waiter_pid_buf, sizeof(waiter_pid_buf), "%d", current ? current->pid : -1);
        snprintf(reaped_pid_buf, sizeof(reaped_pid_buf), "%d", task ? task->pid : -1);
        snprintf(options_buf, sizeof(options_buf), "%d", options);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "waiter_pid", .value = waiter_pid_buf },
            { .key = "reaped_pid", .value = reaped_pid_buf },
            { .key = "options", .value = options_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.wait.reap",
                                      attrs, sizeof(attrs) / sizeof(attrs[0]));
    }

    // tear down group
    cond_destroy(&task->group->child_exit);
    task_leave_session(task);
    list_remove(&task->group->pgroup);
    free(task->group);

    task_destroy(task);
    return true;
}

static bool notify_if_stopped(struct task *task, struct siginfo_ *info_out)
{
    lock(&task->group->lock);
    bool stopped = task->group->stopped;
    unlock(&task->group->lock);
    if (!stopped || task->group->group_exit_code == 0)
        return false;
    uint32_t exit_code = task->group->group_exit_code;
    task->group->group_exit_code = 0;
    info_out->child.status = exit_code;
    return true;
}

static bool reap_if_needed(struct task *task, struct siginfo_ *info_out, struct rusage_ *rusage_out,
                           int options)
{
    assert(task_is_leader(task));
    if ((options & WUNTRACED_ && notify_if_stopped(task, info_out)) ||
        (options & WEXITED_ && reap_if_zombie(task, info_out, rusage_out, options))) {
        info_out->sig = SIGCHLD_;
        return true;
    }
    lock(&task->ptrace.lock);
    if (task->ptrace.stopped && task->ptrace.signal) {
        // I had this code here because it made something work, but it's now
        // making GDB think we support events (we don't). I can't remember what
        // it fixed but until then commenting it out for now.
        info_out->child.status =
            /* task->ptrace.trap_event << 16 |*/ task->ptrace.signal << 8 | 0x7f;
        task->ptrace.signal = 0;
        unlock(&task->ptrace.lock);
        return true;
    }
    unlock(&task->ptrace.lock);
    return false;
}

static bool task_matches_wait_scope(struct task *task, int idtype, pid_t_ id)
{
    if (task == NULL || task->parent == NULL)
        return false;
    bool same_parent = (task->parent == current) ||
                       (task->parent->pid == current->pid) ||
                       (task->parent->tgid != 0 && task->parent->tgid == current->tgid);
    if (!same_parent)
        return false;
    if (!task_is_leader(task))
        return false;
    if (idtype == P_PID_)
        return task->pid == id;
    if (idtype == P_PGID_)
        return task->group->pgid == id;
    return true; // P_ALL_
}

int do_wait(int idtype, pid_t_ id, struct siginfo_ *info, struct rusage_ *rusage, int options)
{
    if (idtype != P_ALL_ && idtype != P_PID_ && idtype != P_PGID_)
        return _EINVAL;
    if (options & ~(WNOHANG_ | WUNTRACED_ | WEXITED_ | WCONTINUED_ | WNOWAIT_ | __WALL_))
        return _EINVAL;

    lock(&pids_lock);
    int err;
    bool no_children = true;
retry:
    no_children = true;
    for (int pid = 1; pid < MAX_PID; pid++) {
        struct task *task = pid_get_task_zombie((uint32_t)pid);
        if (!task_matches_wait_scope(task, idtype, id))
            continue;
        no_children = false;
        info->child.pid = task->pid;
        if (reap_if_needed(task, info, rusage, options))
            goto found_something;
    }

    err = _ECHILD;
    if (no_children) {
        lock(&current->group->lock);
        bool replay = current->group->last_reaped_valid;
        if (replay && idtype == P_PID_ && current->group->last_reaped_pid != id)
            replay = false;
        if (replay && idtype == P_PGID_) {
            struct task *reaped = pid_get_task_zombie((uint32_t)current->group->last_reaped_pid);
            if (reaped == NULL || reaped->group == NULL || reaped->group->pgid != id)
                replay = false;
        }
        if (replay) {
            info->child.pid = current->group->last_reaped_pid;
            info->child.status = current->group->last_reaped_status;
            current->group->last_reaped_valid = false;
            info->sig = SIGCHLD_;
        }
        unlock(&current->group->lock);
        if (replay)
            goto found_something;
        if (options & WNOHANG_) {
            info->child.pid = 0;
            info->sig = SIGCHLD_;
            goto found_something;
        }
        goto error;
    }

    // WNOHANG leaves the info in an implementation-defined state. set the pid
    // to 0 so wait4 can pass that along correctly.
    info->child.pid = 0;
    if (options & WNOHANG_) {
        info->sig = SIGCHLD_;
        goto found_something;
    }

    // no matching zombie found, wait for one
    if (wait_for(&current->group->child_exit, &pids_lock, NULL)) {
        // A pending signal (especially SIGCHLD) may interrupt the wait.
        // Re-scan children first instead of surfacing EINTR immediately.
        goto retry;
    }
    goto retry;

    info->sig = SIGCHLD_;
found_something:
    unlock(&pids_lock);
    return 0;

error:
    if (err == _ECHILD) {
        struct task *probe = pid_get_task_zombie(4);
        char exists_buf[8];
        char zombie_buf[8];
        char probe_parent_buf[32];
        snprintf(exists_buf, sizeof(exists_buf), "%d", probe != NULL ? 1 : 0);
        snprintf(zombie_buf, sizeof(zombie_buf), "%d", (probe && probe->zombie) ? 1 : 0);
        snprintf(probe_parent_buf, sizeof(probe_parent_buf), "%d",
                 (probe && probe->parent) ? probe->parent->pid : -1);
        ixland_instrumentation_attribute_t probe_attrs[] = {
            { .key = "exists", .value = exists_buf },
            { .key = "zombie", .value = zombie_buf },
            { .key = "parent_pid", .value = probe_parent_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.wait.probe4",
                                      probe_attrs, sizeof(probe_attrs) / sizeof(probe_attrs[0]));
        int matching_children = 0;
        struct task *parent;
        list_for_each_entry (&current->group->threads, parent, group_links) {
            struct task *task;
            list_for_each_entry (&parent->children, task, siblings) {
                if (!task_is_leader(task))
                    continue;
                if (idtype == P_PID_ && task->pid != id)
                    continue;
                if (idtype == P_PGID_ && task->group->pgid != id)
                    continue;
                matching_children++;
            }
        }
        char idtype_buf[32];
        char id_buf[32];
        char options_buf[32];
        char matching_buf[32];
        snprintf(idtype_buf, sizeof(idtype_buf), "%d", idtype);
        snprintf(id_buf, sizeof(id_buf), "%d", id);
        snprintf(options_buf, sizeof(options_buf), "%d", options);
        snprintf(matching_buf, sizeof(matching_buf), "%d", matching_children);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "idtype", .value = idtype_buf },
            { .key = "id", .value = id_buf },
            { .key = "options", .value = options_buf },
            { .key = "matching_children", .value = matching_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.wait.echild",
                                      attrs, sizeof(attrs) / sizeof(attrs[0]));
    }
    unlock(&pids_lock);
    return err;
}

uint32_t sys_waitid(int64_t idtype, pid_t_ id, addr_t info_addr, int64_t options)
{
    STRACE("waitid(%d, %d, %#x, %#x)", (int)idtype, id, info_addr, (int)options);
    if ((options & (WEXITED_ | WUNTRACED_ | WCONTINUED_)) == 0)
        return _EINVAL;
    struct siginfo_ info = {};
    int64_t res = do_wait((int)idtype, id, &info, NULL, (int)options);
    if (res < 0 || (res == 0 && info.child.pid == 0)) {
        char idtype_buf[32];
        char id_buf[32];
        char options_buf[32];
        char res_buf[32];
        snprintf(idtype_buf, sizeof(idtype_buf), "%lld", (long long)idtype);
        snprintf(id_buf, sizeof(id_buf), "%d", id);
        snprintf(options_buf, sizeof(options_buf), "%lld", (long long)options);
        snprintf(res_buf, sizeof(res_buf), "%lld", (long long)res);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "idtype", .value = idtype_buf },
            { .key = "id", .value = id_buf },
            { .key = "options", .value = options_buf },
            { .key = "result", .value = res_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                      "guest.waitid.return", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
        if (res == _ECHILD)
            ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.waitid.err_echild");
        else if (res == _EINTR)
            ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.waitid.err_eintr");
        return (uint32_t)res;
    }
    {
        char idtype_buf[32];
        char id_buf[32];
        char options_buf[32];
        char child_pid_buf[32];
        char status_buf[32];
        snprintf(idtype_buf, sizeof(idtype_buf), "%lld", (long long)idtype);
        snprintf(id_buf, sizeof(id_buf), "%d", id);
        snprintf(options_buf, sizeof(options_buf), "%lld", (long long)options);
        snprintf(child_pid_buf, sizeof(child_pid_buf), "%d", info.child.pid);
        snprintf(status_buf, sizeof(status_buf), "%lld", (long long)info.child.status);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "idtype", .value = idtype_buf },
            { .key = "id", .value = id_buf },
            { .key = "options", .value = options_buf },
            { .key = "child_pid", .value = child_pid_buf },
            { .key = "status", .value = status_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                      "guest.waitid.return", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
    }
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.waitid.ok");
    if (info_addr != 0 && user_put(info_addr, info))
        return _EFAULT;
    return 0;
}

uint32_t sys_wait4(pid_t_ id, addr_t status_addr, uint32_t options, addr_t rusage_addr)
{
    STRACE("wait4(%d, %#x, %#x, %#x)", id, status_addr, options, rusage_addr);
    if (options & WNOWAIT_)
        return _EINVAL;

    int idtype;
    if (id > 0)
        idtype = P_PID_;
    else if (id == -1)
        idtype = P_ALL_;
    else {
        idtype = P_PGID_;
        if (id == 0)
            id = current->group->pgid;
        else
            id = -id;
    }

    struct siginfo_ info = { .child.pid = 0xbaba };
    struct rusage_ rusage;
    int64_t res = do_wait(idtype, id, &info, &rusage, options | WEXITED_);
    if (res < 0 || (res == 0 && info.child.pid == 0)) {
        char req_id_buf[32];
        char options_buf[32];
        char res_buf[32];
        snprintf(req_id_buf, sizeof(req_id_buf), "%d", id);
        snprintf(options_buf, sizeof(options_buf), "%u", options);
        snprintf(res_buf, sizeof(res_buf), "%lld", (long long)res);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "request_pid", .value = req_id_buf },
            { .key = "options", .value = options_buf },
            { .key = "result", .value = res_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                      "guest.wait4.return", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
        if (res == _ECHILD)
            ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.wait4.err_echild");
        else if (res == _EINTR)
            ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.wait4.err_eintr");
        return (uint32_t)res;
    }
    {
        char req_id_buf[32];
        char options_buf[32];
        char waited_pid_buf[32];
        char status_buf[32];
        snprintf(req_id_buf, sizeof(req_id_buf), "%d", id);
        snprintf(options_buf, sizeof(options_buf), "%u", options);
        snprintf(waited_pid_buf, sizeof(waited_pid_buf), "%d", info.child.pid);
        snprintf(status_buf, sizeof(status_buf), "%lld", (long long)info.child.status);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "request_pid", .value = req_id_buf },
            { .key = "options", .value = options_buf },
            { .key = "waited_pid", .value = waited_pid_buf },
            { .key = "status", .value = status_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                      "guest.wait4.return", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
    }
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "guest.wait4.ok");
    if (status_addr != 0 && user_put(status_addr, info.child.status))
        return _EFAULT;
    if (rusage_addr != 0 && user_put(rusage_addr, rusage))
        return _EFAULT;
    return info.child.pid;
}

uint32_t sys_waitpid(pid_t_ pid, addr_t status_addr, uint32_t options)
{
    return sys_wait4(pid, status_addr, options, 0);
}
