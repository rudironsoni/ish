#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/task.h"
#include "kernel/memory.h"
#include "emu/tlb.h"
#include "emu/aarch64/cpu.h"
#include "trace/trace.h"

__thread struct task *current;

static struct pid pids[MAX_PID + 1] = {};
lock_t pids_lock = LOCK_INITIALIZER;

static bool pid_empty(struct pid *pid) {
    return pid->task == NULL && list_empty(&pid->session) && list_empty(&pid->pgroup);
}

struct pid *pid_get(dword_t id) {
    if (id > sizeof(pids)/sizeof(pids[0]))
        return NULL;
    struct pid *pid = &pids[id];
    if (pid_empty(pid))
        return NULL;
    return pid;
}

struct task *pid_get_task_zombie(dword_t id) {
    struct pid *pid = pid_get(id);
    if (pid == NULL)
        return NULL;
    struct task *task = pid->task;
    return task;
}

struct task *pid_get_task(dword_t id) {
    struct task *task = pid_get_task_zombie(id);
    if (task != NULL && task->zombie)
        return NULL;
    return task;
}

struct task *task_create_(struct task *parent) {
    lock(&pids_lock);
    static int cur_pid = 0;
    do {
        cur_pid++;
        if (cur_pid > MAX_PID) cur_pid = 1;
    } while (!pid_empty(&pids[cur_pid]));
    struct pid *pid = &pids[cur_pid];
    pid->id = cur_pid;
    list_init(&pid->session);
    list_init(&pid->pgroup);

    struct task *task = malloc(sizeof(struct task));
    if (task == NULL)
        return NULL;
    *task = (struct task) {};
    if (parent != NULL)
        *task = *parent;
    task->pid = pid->id;
    pid->task = task;

    list_init(&task->children);
    list_init(&task->siblings);
    if (parent != NULL) {
        task->parent = parent;
        list_add(&parent->children, &task->siblings);
    }

    task->pending = 0;
    list_init(&task->queue);
    task->clear_tid = 0;
    task->robust_list = 0;
    task->did_exec = false;
    lock_init(&task->general_lock);

    task->sockrestart = (struct task_sockrestart) {};
    list_init(&task->sockrestart.listen);

    task->waiting_cond = NULL;
    task->waiting_lock = NULL;
    lock_init(&task->waiting_cond_lock);
    cond_init(&task->pause);

    lock_init(&task->ptrace.lock);
    cond_init(&task->ptrace.cond);

    // CRITICAL: Memory barrier ensures all task initialization is complete
    // and visible before the lock is released. Without this, the child thread
    // may see partially initialized memory (pid=0, mm=NULL, etc).
    __sync_synchronize();
    unlock(&pids_lock);
    
    // Diagnostic: trace immediately after returning with pid
    trace_emit_task_create_return(task->pid, (uint64_t)task);
    
    return task;
}

void task_destroy(struct task *task) {
    list_remove(&task->siblings);
    pid_get(task->pid)->task = NULL;
    free(task);
}

void task_run_current() {
    trace_emit_task_run_current_entry((uint64_t)current, current ? current->pid : 0);
    trace_emit_task_run_current_mem_check((uint64_t)(current ? current->mm : 0), 
                                          (uint64_t)(current ? current->mem : 0));
    
    // Defensive: check that current and current->mem are valid before proceeding
    if (!current) {
        die("task_run_current: NULL current");
    }
    if (!current->mem) {
        die("task_run_current: NULL current->mem");
    }
    
    struct cpu_state *cpu = &current->cpu;
    struct tlb tlb = {};
    tlb_refresh(&tlb, &current->mem->mmu);
    a64_cpu_run(cpu, &tlb);
    die("a64_cpu_run returned");
}

static void *task_thread(void *task) {
    // Pass task pointer via __thread current to ensure proper visibility
    // The task pointer was fully initialized before task_start was called
    trace_emit_task_thread_entry((uint64_t)task);
    current = task;
    // CRITICAL: Memory barrier ensures all task initialization stores from
    // the parent thread are visible before we read task fields. Without this,
    // the child may see zeroed/corrupted values (pid=0, mm=NULL, etc).
    __sync_synchronize();
    trace_emit_task_thread_current_set(current->pid, (uint64_t)current->mm, (uint64_t)current->mem);
    update_thread_name();
    task_run_current();
    die("task_thread returned");
}

static pthread_attr_t task_thread_attr;
__attribute__((constructor)) static void create_attr() {
    pthread_attr_init(&task_thread_attr);
    pthread_attr_setdetachstate(&task_thread_attr, PTHREAD_CREATE_DETACHED);
}

void task_start(struct task *task) {
    __sync_synchronize();

    // Initialize trace system if not already done
    // This must happen before pthread_create so child thread can emit trace events
    extern trace_ctx_t *g_trace_ctx;
    if (!g_trace_ctx) {
        trace_config_t trace_config;
        trace_config_from_env(&trace_config);
        trace_init(&trace_config);
    }

    // Diagnostic: trace the pointer value and dereferenced pid
    trace_emit_task_start_pointer((uint64_t)task);
    trace_emit_task_start(task ? task->pid : 999999);  // 999999 indicates null task
    if (pthread_create(&task->thread, &task_thread_attr, task_thread, task) < 0)
        die("could not create thread");
}

int_t sys_sched_yield() {
    STRACE("sched_yield()");
    sched_yield();
    return 0;
}

void update_thread_name() {
    char name[16]; // As long as Linux will let us make this
    snprintf(name, sizeof(name), "-%d", current->pid);
    size_t pid_width = strlen(name);
    size_t name_width = snprintf(name, sizeof(name), "%s", current->comm);
    sprintf(name + (name_width < sizeof(name) - 1 - pid_width ? name_width : sizeof(name) - 1 - pid_width), "-%d", current->pid);
#if __APPLE__
    pthread_setname_np(name);
#else
    pthread_setname_np(pthread_self(), name);
#endif
}
