#define _GNU_SOURCE
#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__thread struct task *current;

static _Atomic int64_t g_trace_attempt_id = 0;
static _Atomic int64_t g_trace_guest_pid = 0;
static _Atomic int64_t g_trace_ui_pid = 0;
static _Atomic int g_trace_is_restart = 0;
static _Atomic int g_trace_has_terminal = 0;

void ixland_guest_trace_set_context(int64_t attempt_id, int64_t guest_pid, int64_t ui_pid,
                                    bool is_restart_path, bool has_terminal)
{
    atomic_store(&g_trace_attempt_id, attempt_id);
    atomic_store(&g_trace_guest_pid, guest_pid);
    atomic_store(&g_trace_ui_pid, ui_pid);
    atomic_store(&g_trace_is_restart, is_restart_path ? 1 : 0);
    atomic_store(&g_trace_has_terminal, has_terminal ? 1 : 0);
}

void ixland_guest_trace_set_guest_pid(int64_t guest_pid)
{
    atomic_store(&g_trace_guest_pid, guest_pid);
}

static uint32_t ixland_guest_trace_base_attrs(ixland_instrumentation_attribute_t *attrs,
                                              char *attempt_buf, size_t attempt_len,
                                              char *guest_pid_buf, size_t guest_pid_len,
                                              char *ui_pid_buf, size_t ui_pid_len,
                                              char *restart_buf, size_t restart_len,
                                              char *terminal_buf, size_t terminal_len)
{
    snprintf(attempt_buf, attempt_len, "%lld", (long long)atomic_load(&g_trace_attempt_id));
    snprintf(guest_pid_buf, guest_pid_len, "%lld", (long long)atomic_load(&g_trace_guest_pid));
    snprintf(ui_pid_buf, ui_pid_len, "%lld", (long long)atomic_load(&g_trace_ui_pid));
    snprintf(restart_buf, restart_len, "%d", atomic_load(&g_trace_is_restart));
    snprintf(terminal_buf, terminal_len, "%d", atomic_load(&g_trace_has_terminal));

    attrs[0] = (ixland_instrumentation_attribute_t){ .key = "attempt", .value = attempt_buf };
    attrs[1] = (ixland_instrumentation_attribute_t){ .key = "guest_pid", .value = guest_pid_buf };
    attrs[2] = (ixland_instrumentation_attribute_t){ .key = "ui_pid", .value = ui_pid_buf };
    attrs[3] =
        (ixland_instrumentation_attribute_t){ .key = "is_restart_path", .value = restart_buf };
    attrs[4] = (ixland_instrumentation_attribute_t){ .key = "has_terminal", .value = terminal_buf };
    return 5;
}

void ixland_guest_trace_emit(ixland_instrumentation_origin_t origin, const char *event_name)
{
    char attempt_buf[32], guest_pid_buf[32], ui_pid_buf[32], restart_buf[8], terminal_buf[8];
    ixland_instrumentation_attribute_t attrs[5];
    uint32_t count = ixland_guest_trace_base_attrs(
        attrs, attempt_buf, sizeof(attempt_buf), guest_pid_buf, sizeof(guest_pid_buf), ui_pid_buf,
        sizeof(ui_pid_buf), restart_buf, sizeof(restart_buf), terminal_buf, sizeof(terminal_buf));
    uint64_t interval = ixland_instrumentation_begin_interval(origin, event_name, attrs, count);
    ixland_instrumentation_end_interval(interval, NULL, 0);
}

void ixland_guest_trace_emit_int(ixland_instrumentation_origin_t origin, const char *event_name,
                                 const char *key, int64_t value)
{
    char attempt_buf[32], guest_pid_buf[32], ui_pid_buf[32], restart_buf[8], terminal_buf[8],
        value_buf[32];
    ixland_instrumentation_attribute_t attrs[6];
    uint32_t count = ixland_guest_trace_base_attrs(
        attrs, attempt_buf, sizeof(attempt_buf), guest_pid_buf, sizeof(guest_pid_buf), ui_pid_buf,
        sizeof(ui_pid_buf), restart_buf, sizeof(restart_buf), terminal_buf, sizeof(terminal_buf));
    snprintf(value_buf, sizeof(value_buf), "%lld", (long long)value);
    attrs[count++] = (ixland_instrumentation_attribute_t){ .key = key, .value = value_buf };
    uint64_t interval = ixland_instrumentation_begin_interval(origin, event_name, attrs, count);
    ixland_instrumentation_end_interval(interval, NULL, 0);
}

void ixland_guest_trace_emit_int2(ixland_instrumentation_origin_t origin, const char *event_name,
                                  const char *key1, int64_t value1, const char *key2,
                                  int64_t value2)
{
    char attempt_buf[32], guest_pid_buf[32], ui_pid_buf[32], restart_buf[8], terminal_buf[8];
    char value1_buf[32], value2_buf[32];
    ixland_instrumentation_attribute_t attrs[7];
    uint32_t count = ixland_guest_trace_base_attrs(
        attrs, attempt_buf, sizeof(attempt_buf), guest_pid_buf, sizeof(guest_pid_buf), ui_pid_buf,
        sizeof(ui_pid_buf), restart_buf, sizeof(restart_buf), terminal_buf, sizeof(terminal_buf));
    snprintf(value1_buf, sizeof(value1_buf), "%lld", (long long)value1);
    snprintf(value2_buf, sizeof(value2_buf), "%lld", (long long)value2);
    attrs[count++] = (ixland_instrumentation_attribute_t){ .key = key1, .value = value1_buf };
    attrs[count++] = (ixland_instrumentation_attribute_t){ .key = key2, .value = value2_buf };
    uint64_t interval = ixland_instrumentation_begin_interval(origin, event_name, attrs, count);
    ixland_instrumentation_end_interval(interval, NULL, 0);
}

void ixland_guest_trace_emit_attrs(ixland_instrumentation_origin_t origin, const char *event_name,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count)
{
    char attempt_buf[32], guest_pid_buf[32], ui_pid_buf[32], restart_buf[8], terminal_buf[8];
    ixland_instrumentation_attribute_t base_attrs[5];
    ixland_instrumentation_attribute_t merged_attrs[16];
    uint32_t base_count = ixland_guest_trace_base_attrs(
        base_attrs, attempt_buf, sizeof(attempt_buf), guest_pid_buf, sizeof(guest_pid_buf),
        ui_pid_buf, sizeof(ui_pid_buf), restart_buf, sizeof(restart_buf), terminal_buf,
        sizeof(terminal_buf));
    uint32_t extra_count = attr_count;
    if (extra_count > (uint32_t)(16 - (int)base_count)) {
        extra_count = (uint32_t)(16 - (int)base_count);
    }

    for (uint32_t i = 0; i < base_count; i++) {
        merged_attrs[i] = base_attrs[i];
    }
    for (uint32_t i = 0; i < extra_count; i++) {
        merged_attrs[base_count + i] = attrs[i];
    }

    uint64_t interval = ixland_instrumentation_begin_interval(origin, event_name, merged_attrs,
                                                              base_count + extra_count);
    ixland_instrumentation_end_interval(interval, NULL, 0);
}

void ixland_guest_trace_emit_structured(ixland_instrumentation_origin_t origin,
                                        const char *event_name,
                                        const ixland_guest_trace_field_t *fields,
                                        uint32_t field_count)
{
    if (!fields || field_count == 0) {
        ixland_guest_trace_emit(origin, event_name);
        return;
    }

    if (field_count > 24) {
        field_count = 24;
    }

    char value_bufs[24][32];
    ixland_instrumentation_attribute_t attrs[24];
    for (uint32_t i = 0; i < field_count; i++) {
        attrs[i].key = fields[i].key;
        switch (fields[i].kind) {
        case IXLAND_GUEST_TRACE_FIELD_STRING:
            attrs[i].value = fields[i].string_value ? fields[i].string_value : "";
            break;
        case IXLAND_GUEST_TRACE_FIELD_I64_DEC:
            snprintf(value_bufs[i], sizeof(value_bufs[i]), "%lld", (long long)fields[i].i64_value);
            attrs[i].value = value_bufs[i];
            break;
        case IXLAND_GUEST_TRACE_FIELD_U64_DEC:
            snprintf(value_bufs[i], sizeof(value_bufs[i]), "%llu",
                     (unsigned long long)fields[i].u64_value);
            attrs[i].value = value_bufs[i];
            break;
        case IXLAND_GUEST_TRACE_FIELD_U64_HEX:
            snprintf(value_bufs[i], sizeof(value_bufs[i]), "0x%llx",
                     (unsigned long long)fields[i].u64_value);
            attrs[i].value = value_bufs[i];
            break;
        }
    }

    ixland_guest_trace_emit_attrs(origin, event_name, attrs, field_count);
}

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
    uint64_t actual = task_canary_value;
    uint64_t host_tid = (uint64_t)pthread_self();
    trace_emit_task_canary(task_ptr, actual, 1, host_tid);
    return actual;
}

static void task_start_validation_checkpoint(const char *name, struct task *task)
{
    char pid_buf[32];
    char task_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];

    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(task ? task->pid : 0));
    snprintf(task_buf, sizeof(task_buf), "%p", (void *)task);
    snprintf(mm_buf, sizeof(mm_buf), "%p", task ? (void *)task->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", task ? (void *)task->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", task ? (void *)task->cpu.mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *)&task->mem->mmu : NULL);

    trace_attribute_t attrs[] = {
        { "pid", pid_buf },         { "task", task_buf },
        { "mm", mm_buf },           { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf }, { "expected.mem.mmu", expected_mem_mmu_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static void task_start_validation_checkpoint_with_pthread(const char *name, struct task *task,
                                                          int pthread_ret)
{
    char pid_buf[32];
    char task_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];
    char pthread_ret_buf[32];

    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(task ? task->pid : 0));
    snprintf(task_buf, sizeof(task_buf), "%p", (void *)task);
    snprintf(mm_buf, sizeof(mm_buf), "%p", task ? (void *)task->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", task ? (void *)task->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", task ? (void *)task->cpu.mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *)&task->mem->mmu : NULL);
    snprintf(pthread_ret_buf, sizeof(pthread_ret_buf), "%d", pthread_ret);

    trace_attribute_t attrs[] = {
        { "pid", pid_buf },
        { "task", task_buf },
        { "mm", mm_buf },
        { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf },
        { "expected.mem.mmu", expected_mem_mmu_buf },
        { "pthread_ret", pthread_ret_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static void task_cpu_run_checkpoint(const char *name, struct task *task)
{
    char pid_buf[32];
    char task_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];
    char pc_buf[32];

    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(task ? task->pid : 0));
    snprintf(task_buf, sizeof(task_buf), "%p", (void *)task);
    snprintf(mm_buf, sizeof(mm_buf), "%p", task ? (void *)task->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", task ? (void *)task->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", task ? (void *)task->cpu.mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *)&task->mem->mmu : NULL);
    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", task ? (unsigned long long)task->cpu.pc : 0ULL);

    trace_attribute_t attrs[] = {
        { "pid", pid_buf },         { "task", task_buf },
        { "mm", mm_buf },           { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf }, { "expected.mem.mmu", expected_mem_mmu_buf },
        { "pc", pc_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
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
    // DIAGNOSTIC: First executable line inside task_run_current
    trace_emit_task_proof_point(TASK_PROOF_TASK_RUN_CURRENT_ENTRY, current ? current->pid : 0);
    ixland_guest_trace_set_guest_pid(current ? current->pid : -1);
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_TASK, "guest.task_run_current.entry");
    task_start_validation_checkpoint("task.proof.task_run_current.entry", current);

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
    task_cpu_run_checkpoint("task.proof.task_run_current.before_cpu_run", current);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &current->mem->mmu);

    {
        char pc_buf[32];
        char sp_buf[32];
        snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);
        snprintf(sp_buf, sizeof(sp_buf), "0x%llx", (unsigned long long)cpu->sp);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "guest_pc", .value = pc_buf },
            { .key = "sp", .value = sp_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_TASK, "guest.run.entry", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
    }

    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.a64_cpu_run.entry");
    a64_cpu_run(cpu, &tlb);
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.a64_cpu_run.return");
    task_cpu_run_checkpoint("task.proof.process_terminating", current);
    die("a64_cpu_run returned");
}

static void *task_thread(void *task)
{
    // Get host thread ID for correlation
    uint64_t host_thread_id = (uint64_t)pthread_self();
    struct task *task_arg_early = (struct task *)task;
    ixland_guest_trace_set_guest_pid(task_arg_early ? task_arg_early->pid : -1);
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_TASK, "guest.task_thread.entry");


    // PROOF POINT #4: task_thread() entry - first line
    trace_emit_task_proof_point(TASK_PROOF_THREAD_ENTRY, task_arg_early ? task_arg_early->pid : 0);
    task_start_validation_checkpoint("task.proof.thread_entry", task_arg_early);

    // DIAGNOSTIC: Paired proof point immediately after TASK_THREAD_ENTRY
    trace_emit_task_proof_point(TASK_PROOF_AFTER_THREAD_ENTRY,
                                task_arg_early ? task_arg_early->pid : 0);

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

    // PROOF POINT: Before current = task assignment - captures child-side values
    struct task *task_arg_for_proof = (struct task *)task;
    trace_emit_task_proof_point(TASK_PROOF_BEFORE_CURRENT_SET,
                                task_arg_for_proof ? task_arg_for_proof->pid : 0);
    trace_emit_task_handoff_parent_pre_create(
        (uint64_t)task_arg_for_proof,
        task_arg_for_proof ? (uint64_t)task_arg_for_proof->mm : 0xDEAD,
        task_arg_for_proof ? (uint64_t)task_arg_for_proof->mem : 0xDEAD,
        task_arg_for_proof ? task_arg_for_proof->pid : 0xDEAD, host_thread_id);

    current = task;

    // PROOF POINT #5: After current = task
    trace_emit_task_proof_point(TASK_PROOF_AFTER_CURRENT_SET, current ? current->pid : 0);
    task_start_validation_checkpoint("task.proof.after_current_set", current);

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

    // DIAGNOSTIC: Proof point immediately before calling task_run_current
    trace_emit_task_proof_point(TASK_PROOF_BEFORE_TASK_RUN_CURRENT, current ? current->pid : 0);
    task_start_validation_checkpoint("task.proof.before_task_run_current", current);

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
    task_start_validation_checkpoint("task.proof.start_enter", task);

    // STEP 3: Validate child state before starting thread
    if (task->pid == 0) {
        die("task_start: task->pid is 0");
    }
    task_start_validation_checkpoint("task.proof.check_pid_ok", task);
    if (task->mm == NULL) {
        die("task_start: task->mm is NULL - caller must use task_set_mm()");
    }
    task_start_validation_checkpoint("task.proof.check_mm_ok", task);
    if (task->mem == NULL) {
        die("task_start: task->mem is NULL - caller must use task_set_mm()");
    }
    task_start_validation_checkpoint("task.proof.check_mem_ok", task);
    if (task->cpu.mmu != &task->mem->mmu) {
        die("task_start: cpu.mmu does not match task->mem->mmu");
    }
    task_start_validation_checkpoint("task.proof.check_cpu_mmu_ok", task);

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
    task_start_validation_checkpoint("task.proof.before_pthread", task);

    int pthread_err = pthread_create(&task->thread, &task_thread_attr, task_thread, task);
    if (pthread_err < 0)
        die("could not create thread");

    // PROOF POINT #3: Right after pthread_create returns
    trace_emit_task_proof_point(TASK_PROOF_AFTER_PTHREAD, task ? task->pid : 0);
    task_start_validation_checkpoint_with_pthread("task.proof.after_pthread", task, pthread_err);
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
