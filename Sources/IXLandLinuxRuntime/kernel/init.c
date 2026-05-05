#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/fs/real.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/personality.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int mount_root(const struct fs_ops *fs, const char *source)
{
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.mount_root.entry");
    ixland_guest_trace_field_t mr_fields[] = {
        { .key = "source",
          .kind = IXLAND_GUEST_TRACE_FIELD_STRING,
          .string_value = (char *)source },
    };
    ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                       "boot.mount_root.entry", mr_fields,
                                       sizeof(mr_fields) / sizeof(mr_fields[0]));
    char source_realpath[MAX_PATH + 1];
    if (realpath(source, source_realpath) == NULL) {
        ixland_guest_trace_field_t fields[] = {
            { .key = "source",
              .kind = IXLAND_GUEST_TRACE_FIELD_STRING,
              .string_value = (char *)source },
            { .key = "errno",
              .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
              .i64_value = (int64_t)errno },
        };
        ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                           "boot.mount_root.exit", fields,
                                           sizeof(fields) / sizeof(fields[0]));
        return errno_map();
    }
    int err = do_mount(fs, source_realpath, "", "", 0);
    if (err < 0) {
        ixland_guest_trace_field_t fields[] = {
            { .key = "source",
              .kind = IXLAND_GUEST_TRACE_FIELD_STRING,
              .string_value = (char *)source_realpath },
            { .key = "return_value",
              .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
              .i64_value = (int64_t)err },
        };
        ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                           "boot.mount_root.exit", fields,
                                           sizeof(fields) / sizeof(fields[0]));
        return err;
    }
    if (!mounts_is_non_empty()) {
        ixland_guest_trace_field_t fields[] = {
            { .key = "source",
              .kind = IXLAND_GUEST_TRACE_FIELD_STRING,
              .string_value = (char *)source_realpath },
            { .key = "return_value",
              .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
              .i64_value = (int64_t)_ENODEV },
            { .key = "mounts_non_empty",
              .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
              .i64_value = (int64_t)0 },
        };
        ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                           "boot.mount_root.exit", fields,
                                           sizeof(fields) / sizeof(fields[0]));
        return _ENODEV;
    }
    ixland_guest_trace_field_t ok_fields[] = {
        { .key = "source",
          .kind = IXLAND_GUEST_TRACE_FIELD_STRING,
          .string_value = (char *)source_realpath },
        { .key = "return_value",
          .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
          .i64_value = (int64_t)0 },
        { .key = "mounts_non_empty",
          .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
          .i64_value = (int64_t)1 },
    };
    ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "boot.mount_root.exit",
                                       ok_fields, sizeof(ok_fields) / sizeof(ok_fields[0]));
    return 0;
}

static void establish_signal_handlers(void)
{
    extern void sigusr1_handler(int sig);
    struct sigaction sigact;
    sigact.sa_handler = sigusr1_handler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGUSR1);
    sigaction(SIGUSR1, &sigact, NULL);
    signal(SIGPIPE, SIG_IGN);
}

// copied from include/asm-generic/resource.h in the kernel
static struct rlimit_ init_rlimits[16] = {
    [RLIMIT_CPU_] = { RLIM_INFINITY_, RLIM_INFINITY_ },
    [RLIMIT_FSIZE_] = { RLIM_INFINITY_, RLIM_INFINITY_ },
    [RLIMIT_DATA_] = { RLIM_INFINITY_, RLIM_INFINITY_ },
    [RLIMIT_STACK_] = { 8 * 1024 * 1024, RLIM_INFINITY_ },
    [RLIMIT_CORE_] = { 0, RLIM_INFINITY_ },
    [RLIMIT_RSS_] = { RLIM_INFINITY_, RLIM_INFINITY_ },
    [RLIMIT_NPROC_] = { 1024, 1024 },
    [RLIMIT_NOFILE_] = { 1024, 4096 },
    [RLIMIT_MEMLOCK_] = { 64 * 1024, 64 * 1024 },
    [RLIMIT_AS_] = { RLIM_INFINITY_, RLIM_INFINITY_ },
    [RLIMIT_LOCKS_] = { RLIM_INFINITY_, RLIM_INFINITY_ },
    [RLIMIT_SIGPENDING_] = { 1024, 1024 },
    [RLIMIT_MSGQUEUE_] = { 819200, 819200 },
    [RLIMIT_NICE_] = { 0, 0 },
    [RLIMIT_RTPRIO_] = { 0, 0 },
    [RLIMIT_RTTIME_] = { RLIM_INFINITY_, RLIM_INFINITY_ },
};

// TODO error propagation
static struct task *construct_task(struct task *parent)
{
    // TRACE: Enter construct_task - capture parent and expected child relationship
    trace_emit_u64(TRACE_EVENT_TASK_CREATE, 0, (uint64_t)(parent ? parent->pid : 0));
    /* PROBE: mark entry to construct_task for instrumentation visibility */
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.entry");

    struct task *task = task_create_(parent);
    if (task && !IS_ERR(task))
        trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.created_task_ptr");
    if (task == NULL || IS_ERR(task)) {
        int err = task ? (int)PTR_ERR(task) : -ENOMEM;
        /* Emit structured instrumentation for task_create_ failure */
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.construct_task.error", "task_create_failure",
                                    (int64_t)err);
        return ERR_PTR(err);
    }

    // PROOF TRACE: After task_create_ returns, log pid assignment
    uint64_t host_thread_id = (uint64_t)pthread_self();
    trace_emit_task_init_after_pid_write((uint64_t)task, task->pid, host_thread_id);

    struct tgroup *group = malloc(sizeof(struct tgroup));
    *group = (struct tgroup){};
    list_init(&group->threads);
    lock_init(&group->lock);
    cond_init(&group->child_exit);
    cond_init(&group->stopped_cond);
    memcpy(group->limits, init_rlimits, sizeof(init_rlimits));
    group->leader = task;
    group->personality = ADDR_NO_RANDOMIZE_;
    list_add(&group->threads, &task->group_links);
    task->group = group;
    task->tgid = task->pid;
    task_setsid(task);

    struct mm *new_mm = mm_new();
    if (new_mm != NULL)
        trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.mm_new_ok");
    if (new_mm == NULL) {
        printk("ERROR: construct_task: mm_new() failed\n");
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.construct_task.error", "mm_new_failure",
                                    (int64_t)-ENOMEM);
        return ERR_PTR(-ENOMEM);
    }
    task_set_mm(task, new_mm);

    // PROOF TRACE: After task_set_mm, log mm and mem assignments
    trace_emit_task_init_after_mm_write((uint64_t)task, (uint64_t)task->mm, host_thread_id);
    trace_emit_task_init_after_mem_write((uint64_t)task, (uint64_t)task->mem, host_thread_id);

    task->sighand = sighand_new();
    task->files = fdtable_new(3); // why is there a 3 here

    task->fs = fs_info_new();
    task->fs->umask = 0022;
    // we'll need to have current set to do the open call
    struct task *old_current = current;
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.before_set_current");
    current = task;
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.after_set_current");

    /* Fast-fail: if there are no mounts yet, avoid attempting any
     * filesystem lookup which would call mount_find and assert when the
     * mounts list is empty. Emit structured instrumentation so tests can
     * detect and recover by mounting a root and retrying. */
    lock(&mounts_lock);
    if (list_empty(&mounts)) {
        int is_testing = getenv("XCTestConfigurationFilePath") != NULL;
        ixland_guest_trace_field_t mf_fields[] = {
            { .key = "mounts_count",
              .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
              .i64_value = (int64_t)0 },
            { .key = "is_testing",
              .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC,
              .i64_value = (int64_t)is_testing },
        };
        ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                           "boot.construct_task.mounts_empty", mf_fields,
                                           sizeof(mf_fields) / sizeof(mf_fields[0]));
        unlock(&mounts_lock);
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.construct_task.error", "mounts_empty", (int64_t)_ENODEV);
        return ERR_PTR(_ENODEV);
    }
    unlock(&mounts_lock);

    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.before_generic_open");
    task->fs->root = generic_open("/", O_RDONLY_, 0);
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.after_generic_open");
    if (IS_ERR(task->fs->root)) {
        int err = (int)PTR_ERR(task->fs->root);
        printk("ERROR: construct_task: generic_open(/) failed with %d\n", err);
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.construct_task.error", "generic_open_root_failure",
                                    (int64_t)err);
        return ERR_PTR(err);
    }
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.root_open_ok");
    task->fs->pwd = fd_retain(task->fs->root);
    current = old_current;

    // Emit trace event for task creation
    trace_emit_task_create(task->pid, parent ? parent->pid : 0);

    // Ensure all writes performed while initializing the task (mm, mem, fs,
    // files, etc.) are visible to other threads before we publish the task
    // pointer. Without a publish barrier, a concurrently-started thread may
    // observe a non-null task pointer but stale or zeroed mm/mem fields which
    // leads to immediate failures when those fields are dereferenced. Emit a
    // full memory barrier here as the minimal correct fix to publish the
    // initialized task state to other CPU/host threads.
    __sync_synchronize();

    // Diagnostic: trace at end of construct_task with all fields
    trace_emit_construct_task_done(task->pid, (uint64_t)task, (uint64_t)task->mm,
                                   (uint64_t)task->mem);

    /* Emit structured instrumentation proving construct_task succeeded and the assigned pid */
    ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "boot.construct_task.success",
                                "assigned_pid", (int64_t)task->pid);
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.construct_task.exit_success");

    return task;
}

int become_first_process(void)
{
    printk("become_first_process: ENTRY\n");

    /* Diagnostic probe: record that become_first_process was entered so we
     * can prove whether PID-1 creation is attempted during boot in UI tests.
     * Use the minimal tracing API which forwards to the app-owned instrumentation
     * bridge. Keep this probe tiny and side-effect free. */
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.become_first_process.entry");

    // now seems like a nice time
    establish_signal_handlers();
    printk("become_first_process: signal handlers established\n");

    // AArch64 block cache is lazily initialized per-MMU in a64_cpu_run()
    // No early global init required

    printk("become_first_process: calling construct_task...\n");
    struct task *task = construct_task(NULL);
    printk("become_first_process: construct_task returned task=%p\n", (void *)task);

    /* Diagnostic probe: record that construct_task returned during PID-1
     * creation. Emit structured data with the return value (errno on
     * failure, 0 on success) and the assigned PID when available so the
     * unified log contains clear evidence of PID-1 creation. */
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.become_first_process.construct_return");

    if (IS_ERR(task)) {
        int err = (int)PTR_ERR(task);
        printk("ERROR: become_first_process: construct_task failed with %d\n", err);
        /* Emit structured guest instrumentation with the errno value */
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.construct_task.error", "err", (int64_t)err);
        /* Also emit a summary event for the construct return with errno */
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.become_first_process.construct_return", "return_value",
                                    (int64_t)err);
        return err;
    }

    printk(
        "become_first_process: setting current = task (task->pid=%d, task->mm=%p, task->mem=%p)\n",
        task->pid, (void *)task->mm, (void *)task->mem);
    current = task;
    printk("become_first_process: current set successfully, current=%p\n", (void *)current);
    /* On success, emit the assigned PID as evidence that PID 1 exists. */
    ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                "boot.become_first_process.construct_return", "return_value",
                                (int64_t)0);
    ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                "boot.become_first_process.construct_return", "assigned_pid",
                                (int64_t)task->pid);

    printk("become_first_process: RETURN 0\n");
    return 0;
}

int become_new_init_child(void)
{
    // CONTRACT: PID 1 must exist before any session can be started
    struct task *init = pid_get_task(1);
    if (init == NULL) {
        /* PID 1 missing at session start. Try to (re-)initialize PID 1 here
         * as a minimal robust recovery: call become_first_process() and
         * re-check the pid table. Emit structured instrumentation so UI
         * tests and logs record the retry and its result. */
        ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                "boot.init_child.no_init_task.attempt_reinit");
        int err = become_first_process();
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.become_first_process.retry.return", "return_value",
                                    (int64_t)err);
        if (err < 0) {
            /* If re-init failed, surface the error to caller */
            ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.become_first_process.retry.failed");
            return err;
        }

        /* Re-check pid table after attempted re-init */
        init = pid_get_task(1);
        if (init == NULL) {
            ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
                                    "boot.init_child.pid1_still_missing_after_retry");
            return -1;
        }
    }

    struct task *task = construct_task(init);
    if (IS_ERR(task))
        return (int)PTR_ERR(task);

    // these are things we definitely don't want to inherit
    task->clear_tid = 0;
    task->vfork = NULL;
    task->blocked = task->pending = task->waiting = 0;
    list_init(&task->queue);
    // TODO: think about whether it would be a good idea to inherit fs_info

    // CRITICAL: Memory barrier BEFORE setting current
    // Ensures all task initialization from construct_task() is visible
    // before the child thread can read current->mm, current->mem, etc.
    __sync_synchronize();

    current = task;

    // CRITICAL: Memory barrier AFTER setting current
    // Ensures the write to current is visible before any potential
    // task_start() call that spawns a child thread
    __sync_synchronize();

    return 0;
}

extern int console_major;
extern int console_minor;
void set_console_device(int major, int minor)
{
    console_major = major;
    console_minor = minor;
}

int create_stdio(const char *file, int major, int minor)
{
    (void)file;
    struct fd *fd = adhoc_fd_create(NULL);
    if (fd == NULL)
        return -_ENOMEM;

    fd->stat.rdev = dev_make(major, minor);
    fd->stat.mode = S_IFCHR | S_IRUSR;
    fd->flags = O_RDWR_;

    int err = dev_open(major, minor, DEV_CHAR, fd);
    if (err < 0) {
        fd_close(fd);
        return err;
    }

    fd->refcount = 0;
    current->files->files[0] = fd_retain(fd);
    current->files->files[1] = fd_retain(fd);
    current->files->files[2] = fd_retain(fd);
    return 0;
}

static struct fd *open_fd_from_actual_fd(int fd_no)
{
    struct fd *fd = adhoc_fd_create(&realfs_fdops);
    if (fd == NULL) {
        return NULL;
    }
    fd->real_fd = fd_no;
    fd->dir = NULL;
    return fd;
}

int create_piped_stdio(void)
{
    if (!(current->files->files[0] = open_fd_from_actual_fd(STDIN_FILENO))) {
        return -1;
    }
    if (!(current->files->files[1] = open_fd_from_actual_fd(STDOUT_FILENO))) {
        return -1;
    }
    if (!(current->files->files[2] = open_fd_from_actual_fd(STDERR_FILENO))) {
        return -1;
    }
    return 0;
}
