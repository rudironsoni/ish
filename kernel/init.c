#include "kernel/init.h"

#include "fs/devices.h"
#include "fs/fd.h"
#include "fs/real.h"
#include "fs/tty.h"
#include "kernel/calls.h"
#include "kernel/personality.h"
#include "trace/trace.h"

#include <signal.h>
#include <string.h>
#include <sys/stat.h>

int mount_root(const struct fs_ops *fs, const char *source)
{
    char source_realpath[MAX_PATH + 1];
    if (realpath(source, source_realpath) == NULL)
        return errno_map();
    int err = do_mount(fs, source_realpath, "", "", 0);
    if (err < 0)
        return err;
    return 0;
}

static void establish_signal_handlers()
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

    struct task *task = task_create_(parent);
    if (task == NULL || IS_ERR(task))
        return ERR_PTR(task ? PTR_ERR(task) : -ENOMEM);

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
    if (new_mm == NULL) {
        printk("ERROR: construct_task: mm_new() failed\n");
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
    current = task;
    task->fs->root = generic_open("/", O_RDONLY_, 0);
    if (IS_ERR(task->fs->root)) {
        int err = PTR_ERR(task->fs->root);
        printk("ERROR: construct_task: generic_open(/) failed with %d\n", err);
        return ERR_PTR(err);
    }
    task->fs->pwd = fd_retain(task->fs->root);
    current = old_current;

    // Emit trace event for task creation
    trace_emit_task_create(task->pid, parent ? parent->pid : 0);

    // Diagnostic: trace at end of construct_task with all fields
    trace_emit_construct_task_done(task->pid, (uint64_t)task, (uint64_t)task->mm,
                                   (uint64_t)task->mem);

    return task;
}

int become_first_process()
{
    printk("become_first_process: ENTRY\n");

    // now seems like a nice time
    establish_signal_handlers();
    printk("become_first_process: signal handlers established\n");

    // AArch64 block cache is lazily initialized per-MMU in a64_cpu_run()
    // No early global init required

    printk("become_first_process: calling construct_task...\n");
    struct task *task = construct_task(NULL);
    printk("become_first_process: construct_task returned task=%p\n", (void *)task);

    if (IS_ERR(task)) {
        printk("ERROR: become_first_process: construct_task failed with %d\n", PTR_ERR(task));
        return PTR_ERR(task);
    }

    printk(
        "become_first_process: setting current = task (task->pid=%d, task->mm=%p, task->mem=%p)\n",
        task->pid, (void *)task->mm, (void *)task->mem);
    current = task;
    printk("become_first_process: current set successfully, current=%p\n", (void *)current);
    printk("become_first_process: RETURN 0\n");
    return 0;
}

int become_new_init_child()
{
    struct task *init = pid_get_task(1);
    if (init == NULL)
        return -1;

    struct task *task = construct_task(init);
    if (IS_ERR(task))
        return PTR_ERR(task);

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
    struct fd *fd = generic_open(file, O_RDWR_, 0);
    if (IS_ERR(fd)) {
        // fallback to adhoc files for stdio
        fd = adhoc_fd_create(NULL);
        fd->stat.rdev = dev_make(major, minor);
        fd->stat.mode = S_IFCHR | S_IRUSR;
        fd->flags = O_RDWR_;
        int err = dev_open(major, minor, DEV_CHAR, fd);
        if (err < 0)
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

int create_piped_stdio()
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
