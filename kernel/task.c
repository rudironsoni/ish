#define _GNU_SOURCE
#include "kernel/task.h"

#include "emu/aarch64/cpu.h"
#include "emu/tlb.h"
#include "kernel/calls.h"
#include "kernel/memory.h"
#include "trace/trace.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

__thread struct task *current;

/* Sidecar canary system for task handoff proof */
#define TASK_CANARY_MAGIC 0xDEADBEEFCAFEBABEULL
static _Atomic uint64_t task_canary_value = 0;
static _Atomic uint64_t task_canary_task_ptr = 0;

void task_canary_write(uint64_t task_ptr)
{
    task_canary_task_ptr = task_ptr;
    task_canary_value = TASK_CANARY_MAGIC ^ task_ptr;
    uint64_t host_tid = (uint64_t)pthread_self();
    trace_emit_task_canary(task_ptr, task_canary_value, 0, host_tid);
}

uint64_t task_canary_read(uint64_t task_ptr)
{
    uint64_t expected = TASK_CANARY_MAGIC ^ task_ptr;
    uint64_t actual = task_canary_value;
    uint64_t host_tid = (uint64_t)pthread_self();
    trace_emit_task_canary(task_ptr, actual, 1, host_tid);
    return actual;
}

static struct pid pids[MAX_PID + 1] = {};
lock_t pids_lock = LOCK_INITIALIZER;

static bool pid_empty(struct pid *pid)
{
    return pid->task == NULL && list_empty(&pid->session) && list_empty(&pid->pgroup);
}

struct pid *pid_get(dword_t id)
{
    if (id > sizeof(pids) / sizeof(pids[0]))
        return NULL;
    struct pid *pid = &pids[id];
    if (pid_empty(pid))
        return NULL;
    return pid;
}

struct task *pid_get_task_zombie(dword_t id)
{
    struct pid *pid = pid_get(id);
    if (pid == NULL)
        return NULL;
    struct task *task = pid->task;
    return task;
}

struct task *pid_get_task(dword_t id)
{
    struct task *task = pid_get_task_zombie(id);
    if (task != NULL && task->zombie)
        return NULL;
    return task;
}

struct task *task_create_(struct task *parent)
{
    trace_emit(TRACE_EVENT_TASK_CREATE, 0);
    lock(&pids_lock);
    static int cur_pid = 0;
    do {
        cur_pid++;
        if (cur_pid > MAX_PID)
            cur_pid = 1;
    } while (!pid_empty(&pids[cur_pid]));
    struct pid *pid = &pids[cur_pid];
    pid->id = cur_pid;
    list_init(&pid->session);
    list_init(&pid->pgroup);

    struct task *task = malloc(sizeof(struct task));
    if (task == NULL)
        return NULL;
    trace_emit_task_create(pid->id, parent ? parent->pid : 0);

    // STEP 1: Zero-initialize the entire task structure
    memset(task, 0, sizeof(struct task));

    // STEP 2: Explicitly inherit only safe inheritable fields from parent
    if (parent != NULL) {
        // Credentials - safe to inherit
        task->uid = parent->uid;
        task->gid = parent->gid;
        task->euid = parent->euid;
        task->egid = parent->egid;
        task->suid = parent->suid;
        task->sgid = parent->sgid;

        // Group membership - safe to inherit
        task->ngroups = parent->ngroups;
        if (task->ngroups > 0) {
            memcpy(task->groups, parent->groups, sizeof(uid_t_) * task->ngroups);
        }

        // Command name - safe to inherit
        strncpy(task->comm, parent->comm, sizeof(task->comm) - 1);
        task->comm[sizeof(task->comm) - 1] = '\0';

        // File descriptor table - shared reference
        task->files = parent->files;

        // Filesystem info - shared reference
        task->fs = parent->fs;

        // Signal handling - shared reference
        task->sighand = parent->sighand;

        // Signal masks - safe to inherit
        task->blocked = parent->blocked;
        task->saved_mask = parent->saved_mask;
        task->has_saved_mask = parent->has_saved_mask;

        // Exit signal - safe to inherit
        task->exit_signal = parent->exit_signal;

        // Vfork info - inherited for vfork semantics
        task->vfork = parent->vfork;

        // Thread group - inherited
        task->group = parent->group;

        // VDSO trampoline - inherited
        task->vdso_sigtramp = parent->vdso_sigtramp;
    }

    // STEP 3: Freshly initialize all runtime-owned fields

    // CPU state - zero-initialized above, will be set by caller
    // MM and mem - MUST be NULL initially, caller must use task_set_mm()
    task->mm = NULL;
    task->mem = NULL;

    // Thread and threadid - will be set by task_start()
    task->thread = 0;
    task->threadid = 0;

    // PID assignment
    task->pid = pid->id;
    pid->task = task;

    // TGID - same as pid for new threads, inherited from parent for threads in group
    if (parent != NULL && parent->group != NULL) {
        task->tgid = parent->tgid;
    } else {
        task->tgid = task->pid;
    }

    // List links - fresh initialization
    list_init(&task->group_links);
    list_init(&task->children);
    list_init(&task->siblings);

    // Parent/child relationships
    if (parent != NULL) {
        task->parent = parent;
        list_add(&parent->children, &task->siblings);
    }

    // Signal state - fresh initialization
    task->pending = 0;
    list_init(&task->queue);
    task->clear_tid = 0;
    task->robust_list = 0;
    task->did_exec = false;

    // Locks - fresh initialization
    lock_init(&task->general_lock);
    lock_init(&task->waiting_cond_lock);
    lock_init(&task->ptrace.lock);

    // Condition variables - fresh initialization
    cond_init(&task->pause);
    cond_init(&task->ptrace.cond);

    // Waiting state - fresh initialization
    task->waiting_cond = NULL;
    task->waiting_lock = NULL;
    task->waiting = 0;

    // Ptrace state - fresh initialization (except inherited traced flag if desired)
    task->ptrace.traced = parent ? parent->ptrace.traced : false;
    task->ptrace.stopped = false;
    task->ptrace.signal = 0;
    memset(&task->ptrace.info, 0, sizeof(task->ptrace.info));
    task->ptrace.trap_event = 0;

    // Exit state - fresh initialization
    task->exit_code = 0;
    task->zombie = false;
    task->exiting = false;

    // Socket restart state - fresh initialization
    task->sockrestart = (struct task_sockrestart){};
    list_init(&task->sockrestart.listen);

    // CRITICAL: Memory barrier ensures all task initialization is complete
    // and visible before the lock is released. Without this, the child thread
    // may see partially initialized memory (pid=0, mm=NULL, etc).
    __sync_synchronize();
    unlock(&pids_lock);

    // STEP 4: Validate child state before returning
    if (task->pid == 0) {
        die("task_create_: pid is 0 after initialization");
    }
    if (parent != NULL) {
        // For child tasks, mm must be set by caller via task_set_mm()
        // We don't check mm here because it should be NULL initially
        // and set explicitly by the caller (fork.c, init.c, etc.)
    }

    // Diagnostic: trace immediately after returning with pid
    trace_emit_task_create_return(task->pid, (uint64_t)task);
    trace_emit_construct_task_done(task->pid, (uint64_t)task, (uint64_t)task->mm,
                                   (uint64_t)task->mem);

    return task;
}

void task_destroy(struct task *task)
{
    list_remove(&task->siblings);
    pid_get(task->pid)->task = NULL;
    free(task);
}

void task_run_current()
{
    // PROOF POINT #6: task_run_current() entry
    trace_emit_task_proof_point(TASK_PROOF_RUN_CURRENT_ENTER, current ? current->pid : 0);

    // TRACE-ONLY: Entry trace with current pointer
    trace_emit_task_run_current_entry_check((uint64_t)current);
    trace_emit_task_run_current_entry((uint64_t)current, current ? current->pid : 0);
    trace_emit_task_run_current_mem_check((uint64_t)(current ? current->mm : 0),
                                          (uint64_t)(current ? current->mem : 0));

    // PROOF TRACE #5: Child entering task_run_current
    uint64_t host_thread_id = (uint64_t)pthread_self();
    trace_emit_task_handoff_child_pre_run(
        (uint64_t)current, current ? (uint64_t)current->mm : 0xDEAD,
        current ? (uint64_t)current->mem : 0xDEAD, current ? current->pid : 0xDEAD, host_thread_id);

    // Defensive: check that current and current->mem are valid before proceeding
    if (!current) {
        die("task_run_current: NULL current");
    }
    if (!current->mem) {
        die("task_run_current: NULL current->mem");
    }

    struct cpu_state *cpu = &current->cpu;

    // CRITICAL: Validate CPU state before entering a64_cpu_run
    // These checks prevent crashes from invalid entry point calculations (e.g., PIE binaries)
    if (!cpu->mmu) {
        die("task_run_current: NULL cpu->mmu - entry point calculation may have failed");
    }
    if (cpu->pc == 0) {
        die("task_run_current: cpu->pc is 0 - ELF entry point not set correctly");
    }
    if (cpu->pc == 0x100000000ULL) {
        die("task_run_current: cpu->pc is at 4GB boundary - PIE bias calculation failed");
    }

    // TRACE-ONLY: CPU state validation passed
    trace_emit_pstate_snapshot((uint64_t)cpu->pc, cpu->pstate, (cpu->pstate >> 28) & 0xF);

    // PROOF POINT #7: Before entering guest CPU execution
    trace_emit_task_proof_point(TASK_PROOF_BEFORE_GUEST_CPU, current ? current->pid : 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &current->mem->mmu);
    a64_cpu_run(cpu, &tlb);
    die("a64_cpu_run returned");
}

static void *task_thread(void *task)
{
    // Get host thread ID for correlation
    uint64_t host_thread_id = (uint64_t)pthread_self();

    // PROOF POINT #4: task_thread() entry - first line
    struct task *task_arg_early = (struct task *)task;
    trace_emit_task_proof_point(TASK_PROOF_THREAD_ENTRY, task_arg_early ? task_arg_early->pid : 0);

    // PROOF TRACE #3: Child thread entry, BEFORE setting current
    // Read directly from task argument (not via current)
    struct task *task_arg = (struct task *)task;
    trace_emit_task_handoff_child_entry((uint64_t)task_arg,
                                        task_arg ? (uint64_t)task_arg->mm : 0xDEAD,
                                        task_arg ? (uint64_t)task_arg->mem : 0xDEAD,
                                        task_arg ? task_arg->pid : 0xDEAD, host_thread_id);

    // MEMORY SNAPSHOT: Child entry before current = task
    uint64_t canary_at_entry = task_canary_read((uint64_t)task_arg);
    trace_emit_task_mem_snapshot((uint64_t)task_arg, 1, canary_at_entry, host_thread_id);

    // Original diagnostic traces
    trace_emit_task_thread_entry((uint64_t)task);
    trace_emit_task_thread_before_set((uint64_t)task, (uint64_t)current);

    current = task;

    // PROOF POINT #5: After current = task
    trace_emit_task_proof_point(TASK_PROOF_AFTER_CURRENT_SET, current ? current->pid : 0);

    trace_emit_task_thread_after_set((uint64_t)task, (uint64_t)current);

    // CRITICAL: Memory barrier ensures all task initialization stores from
    // the parent thread are visible before we read task fields.
    __sync_synchronize();

    // PROOF TRACE #4: Child after setting current, BEFORE task_run_current
    trace_emit_task_handoff_child_post_current(
        (uint64_t)current, current ? (uint64_t)current->mm : 0xDEAD,
        current ? (uint64_t)current->mem : 0xDEAD, current ? current->pid : 0xDEAD, host_thread_id);

    // MEMORY SNAPSHOT: Child after current = task
    uint64_t canary_at_post_current = task_canary_read((uint64_t)current);
    trace_emit_task_mem_snapshot((uint64_t)current, 2, canary_at_post_current, host_thread_id);

    // State validation: these should pass if task_create_ and task_set_mm were correct
    if (current->pid == 0) {
        die("task_thread: current->pid is 0");
    }
    if (current->mm == NULL) {
        die("task_thread: current->mm is NULL");
    }
    if (current->mem == NULL) {
        die("task_thread: current->mem is NULL - parent did not call task_set_mm()");
    }
    if (current->cpu.mmu != &current->mem->mmu) {
        die("task_thread: cpu.mmu does not match current->mem->mmu");
    }

    trace_emit_task_thread_current_set(current->pid, (uint64_t)current->mm, (uint64_t)current->mem);
    update_thread_name();
    task_run_current();
    die("task_thread returned");
}

static pthread_attr_t task_thread_attr;
__attribute__((constructor)) static void create_attr()
{
    pthread_attr_init(&task_thread_attr);
    pthread_attr_setdetachstate(&task_thread_attr, PTHREAD_CREATE_DETACHED);
}

void task_start(struct task *task)
{
    // PROOF POINT #1: task_start() entry
    trace_emit_task_proof_point(TASK_PROOF_START_ENTER, task ? task->pid : 0);

    // STEP 3: Validate child state before starting thread
    if (task->pid == 0) {
        die("task_start: task->pid is 0");
    }
    if (task->mm == NULL) {
        die("task_start: task->mm is NULL - caller must use task_set_mm()");
    }
    if (task->mem == NULL) {
        die("task_start: task->mem is NULL - caller must use task_set_mm()");
    }
    if (task->cpu.mmu != &task->mem->mmu) {
        die("task_start: cpu.mmu does not match task->mem->mmu");
    }

    __sync_synchronize();

    // Trace system is initialized by app layer (app-first bootstrap)
    // Lower layers MUST NOT own trace initialization - only emit events

    // PROOF TRACE #2: Parent immediately BEFORE pthread_create
    uint64_t host_thread_id = (uint64_t)pthread_self();
    trace_emit_task_handoff_parent_pre_create((uint64_t)task, task ? (uint64_t)task->mm : 0xDEAD,
                                              task ? (uint64_t)task->mem : 0xDEAD,
                                              task ? task->pid : 0xDEAD, host_thread_id);

    // PROOF TRACE: Pre-pthread_create with full task state
    trace_emit_task_init_pre_pthread_create((uint64_t)task, task ? (uint64_t)task->mm : 0xDEAD,
                                            task ? (uint64_t)task->mem : 0xDEAD,
                                            task ? task->pid : 0xDEAD, host_thread_id);

    // MEMORY SNAPSHOT: Parent immediately before pthread_create
    task_canary_write((uint64_t)task);
    trace_emit_task_mem_snapshot((uint64_t)task, 0, task_canary_value, host_thread_id);

    // Diagnostic: trace the pointer value and dereferenced pid
    trace_emit_task_start_pointer((uint64_t)task);
    trace_emit_task_start(task ? task->pid : 999999); // 999999 indicates null task

    // PROOF POINT #2: Right before pthread_create
    trace_emit_task_proof_point(TASK_PROOF_BEFORE_PTHREAD, task ? task->pid : 0);

    if (pthread_create(&task->thread, &task_thread_attr, task_thread, task) < 0)
        die("could not create thread");

    // PROOF POINT #3: Right after pthread_create returns
    trace_emit_task_proof_point(TASK_PROOF_AFTER_PTHREAD, task ? task->pid : 0);
}

int_t sys_sched_yield()
{
    STRACE("sched_yield()");
    sched_yield();
    return 0;
}

void update_thread_name()
{
    char name[16]; // As long as Linux will let us make this
    snprintf(name, sizeof(name), "-%d", current->pid);
    size_t pid_width = strlen(name);
    size_t name_width = snprintf(name, sizeof(name), "%s", current->comm);
    sprintf(name + (name_width < sizeof(name) - 1 - pid_width ? name_width
                                                              : sizeof(name) - 1 - pid_width),
            "-%d", current->pid);
#if __APPLE__
    pthread_setname_np(name);
#else
    pthread_setname_np(pthread_self(), name);
#endif
}
