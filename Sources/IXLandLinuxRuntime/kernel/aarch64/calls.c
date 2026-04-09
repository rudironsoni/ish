/*
 * aarch64 syscall dispatch
 *
 * Handles SVC instructions by dispatching to the appropriate syscall handler
 * using the aarch64 Linux syscall ABI:
 *   x8  = syscall number
 *   x0-x5 = arguments
 *   x0  = return value
 */

#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/interrupt.h>
#import <IXLandLinuxRuntime/kernel/aarch64/calls.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#include <stdio.h>
#include <string.h>

// Forward declarations for syscall handlers
extern syscall_t syscall_table[];

// Number of arguments for each syscall (for proper argument marshalling)
// This mirrors the x86 syscall_arg_count in kernel/calls.c
// Initialized at runtime to avoid initializer override warnings
static uint8_t syscall_arg_count_a64[A64_SYS_MAX + 1];

// Initialize syscall argument counts
static void init_syscall_arg_counts(void)
{
    // Set default: all syscalls take 6 args
    for (int i = 0; i <= A64_SYS_MAX; i++) {
        syscall_arg_count_a64[i] = 6;
    }
    // Override specific syscalls
    syscall_arg_count_a64[A64_SYS_exit] = 1;
    syscall_arg_count_a64[A64_SYS_exit_group] = 1;
    syscall_arg_count_a64[A64_SYS_read] = 3;
    syscall_arg_count_a64[A64_SYS_write] = 3;
    syscall_arg_count_a64[A64_SYS_openat] = 4;
    syscall_arg_count_a64[A64_SYS_close] = 1;
    syscall_arg_count_a64[A64_SYS_brk] = 1;
    syscall_arg_count_a64[A64_SYS_getpid] = 0;
    syscall_arg_count_a64[A64_SYS_getppid] = 0;
    syscall_arg_count_a64[A64_SYS_getuid] = 0;
    syscall_arg_count_a64[A64_SYS_getgid] = 0;
    syscall_arg_count_a64[A64_SYS_geteuid] = 0;
    syscall_arg_count_a64[A64_SYS_getegid] = 0;
}

static void trace_handle_interrupt_checkpoint(const char *name, int interrupt, int signal_code)
{
    struct cpu_state *cpu = current ? &current->cpu : NULL;
    char task_buf[32];
    char pid_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];
    char pc_buf[32];
    char fault_addr_buf[32];
    char fault_write_buf[32];
    char interrupt_buf[32];
    char signal_code_buf[32];

    snprintf(task_buf, sizeof(task_buf), "%p", (void *)current);
    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(current ? current->pid : 0));
    snprintf(mm_buf, sizeof(mm_buf), "%p", current ? (void *)current->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", current ? (void *)current->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", cpu ? (void *)cpu->mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             current && current->mem ? (void *)&current->mem->mmu : NULL);
    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", cpu ? (unsigned long long)cpu->pc : 0ULL);
    snprintf(fault_addr_buf, sizeof(fault_addr_buf), "0x%llx",
             cpu ? (unsigned long long)cpu->fault_addr : 0ULL);
    snprintf(fault_write_buf, sizeof(fault_write_buf), "%d", cpu ? cpu->fault_was_write : 0);
    snprintf(interrupt_buf, sizeof(interrupt_buf), "%d", interrupt);
    snprintf(signal_code_buf, sizeof(signal_code_buf), "%d", signal_code);

    trace_attribute_t attrs[] = {
        { "task", task_buf },
        { "pid", pid_buf },
        { "mm", mm_buf },
        { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf },
        { "expected.mem.mmu", expected_mem_mmu_buf },
        { "pc", pc_buf },
        { "fault_addr", fault_addr_buf },
        { "fault_write", fault_write_buf },
        { "interrupt", interrupt_buf },
        { "signal_code", signal_code_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

/*
 * Stage 3A.5: First post-exec syscall capture
 * Static flag to track first syscall after exec
 */
static int post_exec_first_syscall = 1;

/*
 * Handle an aarch64 syscall (SVC #0)
 * Called from the execution loop when SVC is encountered
 */
void a64_handle_syscall(struct cpu_state *cpu)
{
    // aarch64 syscall ABI:
    // x8 = syscall number
    // x0-x5 = arguments
    // x0 = return value (overwritten)
    // C flag in PSTATE = error indicator

    uint64_t syscall_num = cpu->x[8];
    uint32_t pid = current ? current->pid : 0;

    // Stage 3A.5: Capture FIRST syscall after exec - ENTRY
    // This proves instrumentation is on the REAL AArch64 syscall path
    if (trace_is_active()) {
        char syscall_num_buf[32];
        char pid_buf[32];
        char x0_buf[32], x1_buf[32], x2_buf[32];
        char fd_buf[32];
        char is_first_buf[8];

        snprintf(syscall_num_buf, sizeof(syscall_num_buf), "%llu", (unsigned long long)syscall_num);
        snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)pid);
        snprintf(x0_buf, sizeof(x0_buf), "0x%llx", (unsigned long long)cpu->x[0]);
        snprintf(x1_buf, sizeof(x1_buf), "0x%llx", (unsigned long long)cpu->x[1]);
        snprintf(x2_buf, sizeof(x2_buf), "0x%llx", (unsigned long long)cpu->x[2]);
        // For read/write/ioctl, x0 is the fd
        snprintf(fd_buf, sizeof(fd_buf), "%llu", (unsigned long long)cpu->x[0]);
        snprintf(is_first_buf, sizeof(is_first_buf), "%d", post_exec_first_syscall);

        trace_attribute_t entry_attrs[] = {
            { "task.proof.guest.syscall.number", syscall_num_buf },
            { "task.proof.guest.syscall.pid", pid_buf },
            { "task.proof.guest.syscall.arg0", x0_buf },
            { "task.proof.guest.syscall.arg1", x1_buf },
            { "task.proof.guest.syscall.arg2", x2_buf },
            { "task.proof.guest.syscall.fd", fd_buf },
            { "task.proof.guest.syscall.is_first_post_exec", is_first_buf },
        };

        trace_begin_interval(TRACE_ORIGIN_TASK, "task.proof.guest.syscall.entry", entry_attrs,
                             sizeof(entry_attrs) / sizeof(entry_attrs[0]));

        // If this is the first syscall after exec, emit special marker
        if (post_exec_first_syscall) {
            trace_record_event(TRACE_ORIGIN_TASK, "task.proof.guest.syscall.first_after_exec");
            // Don't clear post_exec_first_syscall here - let it capture all syscalls until
            // explicitly reset
        }
    }

    // Validate syscall number
    if (syscall_num >= A64_SYS_MAX || syscall_table_a64[syscall_num] == NULL) {
        // Unknown syscall - return ENOSYS
        cpu->x[0] = -ENOSYS;
        cpu->c = 1; // Set carry flag to indicate error

        // Stage 3A.5: Record ENOSYS return
        if (trace_is_active()) {
            char ret_buf[32];
            snprintf(ret_buf, sizeof(ret_buf), "-ENOSYS");
            trace_attribute_t enosys_attrs[] = {
                { "task.proof.guest.syscall.return", ret_buf },
                { "task.proof.guest.syscall.status", "ENOSYS" },
            };
            trace_begin_interval(TRACE_ORIGIN_TASK, "task.proof.guest.syscall.return_enosys",
                                 enosys_attrs, sizeof(enosys_attrs) / sizeof(enosys_attrs[0]));
        }
        return;
    }

    a64_syscall_t handler = syscall_table_a64[syscall_num];
    // num_args unused but kept for future use
    (void)syscall_arg_count_a64[syscall_num];

    // Extract arguments based on count
    // Note: For simplicity, we call with 6 args always
    // The handler will use only what it needs
    uint64_t args[6] = { cpu->x[0], cpu->x[1], cpu->x[2], cpu->x[3], cpu->x[4], cpu->x[5] };

    // Call the handler
    uint64_t ret = handler(args[0], args[1], args[2], args[3], args[4], args[5]);

    // Store return value
    cpu->x[0] = ret;

    // Set carry flag if error (negative return value)
    // Note: In Linux, errors are indicated by -4096 < ret < 0
    // but for simplicity, we just check sign
    if ((int64_t)ret < 0 && (int64_t)ret >= -4095) {
        cpu->c = 1;
    } else {
        cpu->c = 0;
    }

    // Stage 3A.5: Capture syscall RETURN
    if (trace_is_active()) {
        char ret_buf[32];
        char errno_buf[32];
        char blocking_buf[16];

        snprintf(ret_buf, sizeof(ret_buf), "%lld", (long long)ret);
        if ((int64_t)ret < 0 && (int64_t)ret >= -4095) {
            snprintf(errno_buf, sizeof(errno_buf), "%lld", (long long)-ret);
        } else {
            snprintf(errno_buf, sizeof(errno_buf), "0");
        }
        // Check if this looks like a blocking return (EAGAIN, EINTR, or positive values for partial
        // reads)
        int64_t sret = (int64_t)ret;
        if (sret < 0 && (-sret == 11 || -sret == 4)) { // EAGAIN=11, EINTR=4
            snprintf(blocking_buf, sizeof(blocking_buf), "BLOCKING");
        } else if (sret == 0 && (syscall_num == A64_SYS_read || syscall_num == A64_SYS_write)) {
            snprintf(blocking_buf, sizeof(blocking_buf), "EOF");
        } else {
            snprintf(blocking_buf, sizeof(blocking_buf), "OK");
        }

        trace_attribute_t return_attrs[] = {
            { "task.proof.guest.syscall.return", ret_buf },
            { "task.proof.guest.syscall.errno", errno_buf },
            { "task.proof.guest.syscall.blocking_status", blocking_buf },
        };

        trace_begin_interval(TRACE_ORIGIN_TASK, "task.proof.guest.syscall.return", return_attrs,
                             sizeof(return_attrs) / sizeof(return_attrs[0]));
    }

    // Advance PC past the SVC instruction
    cpu->pc += 4;
}

/*
 * Initialize syscall dispatch tables
 * Called once during system initialization
 */
void a64_syscall_init(void)
{
    // Initialize syscall argument counts
    init_syscall_arg_counts();

    // Validate that all expected syscalls are mapped
    // This helps catch mismatches between syscall numbers and handlers

    // Critical syscalls that must be present
    static const uint64_t critical_syscalls[] = {
        A64_SYS_exit,  A64_SYS_exit_group, A64_SYS_read, A64_SYS_write,  A64_SYS_openat,
        A64_SYS_close, A64_SYS_brk,        A64_SYS_mmap, A64_SYS_munmap,
    };

    int missing = 0;
    for (size_t i = 0; i < sizeof(critical_syscalls) / sizeof(critical_syscalls[0]); i++) {
        uint64_t num = critical_syscalls[i];
        if (num >= A64_SYS_MAX || syscall_table_a64[num] == NULL) {
            char ev[128];
            snprintf(ev, sizeof(ev), "guest.syscall_table.critical_missing=%llu", num);
            trace_record_event(TRACE_ORIGIN_TASK, ev);
            missing++;
        }
    }

    if (missing > 0) {
        char ev[128];
        snprintf(ev, sizeof(ev), "guest.syscall_table.missing_total=%d", missing);
        trace_record_event(TRACE_ORIGIN_TASK, ev);
    }
}

/*
 * Get syscall name for debugging
 */
const char *a64_syscall_name(qword_t num)
{
    if (num >= A64_SYS_MAX)
        return "unknown";

    static const char *syscall_names[] = {
        [A64_SYS_read] = "read",       [A64_SYS_write] = "write",
        [A64_SYS_openat] = "openat",   [A64_SYS_close] = "close",
        [A64_SYS_exit] = "exit",       [A64_SYS_exit_group] = "exit_group",
        [A64_SYS_brk] = "brk",         [A64_SYS_mmap] = "mmap",
        [A64_SYS_munmap] = "munmap",   [A64_SYS_getpid] = "getpid",
        [A64_SYS_getppid] = "getppid", [A64_SYS_getuid] = "getuid",
        [A64_SYS_getgid] = "getgid",
    };

    if (num >= sizeof(syscall_names) / sizeof(syscall_names[0]))
        return "unknown";
    if (syscall_names[num])
        return syscall_names[num];

    return "unknown";
}

/*
 * Dump syscall arguments for debugging
 */

// AArch64 interrupt handling
// INT_SYSCALL (128) - Syscall via SVC instruction
// INT_GPF (13)      - General protection fault
void handle_interrupt(int interrupt)
{
    struct cpu_state *cpu = &current->cpu;
    trace_handle_interrupt_checkpoint("task.proof.handle_interrupt.entry", interrupt, 0);
    ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_TASK, "guest.handle_interrupt.entry",
                                "interrupt", interrupt);

    switch (interrupt) {
    case INT_SYSCALL:
        // Syscalls are dispatched via a64_do_syscall
        // This path is reached when SVC triggers an exception
        // The actual syscall dispatch happens in the TCTI exit path
        break;

    case INT_GPF: {
        // Trace fault decode via approved instrumentation only
        trace_handle_interrupt_checkpoint("task.proof.handle_interrupt.after_fault_decode",
                                          interrupt, 0);

        // Page fault - try to resolve via mem_ptr (handles stack growth, CoW, etc.)
        read_wrlock(&current->mem->lock);
        void *ptr =
            mem_ptr(current->mem, cpu->fault_addr, cpu->fault_was_write ? MEM_WRITE : MEM_READ);
        read_wrunlock(&current->mem->lock);

        if (ptr == NULL) {
            // Page fault could not be resolved - deliver SIGSEGV
            struct siginfo_ info = {
                .code = mem_segv_reason(current->mem, cpu->fault_addr),
                .fault.addr = cpu->fault_addr,
            };

            {
                char pc_buf[32];
                char fault_addr_buf[32];
                char sig_buf[16];
                char sig_code_buf[16];
                snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);
                snprintf(fault_addr_buf, sizeof(fault_addr_buf), "0x%llx",
                         (unsigned long long)cpu->fault_addr);
                snprintf(sig_buf, sizeof(sig_buf), "%d", SIGSEGV_);
                snprintf(sig_code_buf, sizeof(sig_code_buf), "%d", info.code);
                ixland_instrumentation_attribute_t attrs[] = {
                    { .key = "guest_pc", .value = pc_buf },
                    { .key = "fault_addr", .value = fault_addr_buf },
                    { .key = "signal", .value = sig_buf },
                    { .key = "signal_code", .value = sig_code_buf },
                };
                ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_TASK,
                                              "guest.signal.raise", attrs,
                                              sizeof(attrs) / sizeof(attrs[0]));
            }

            deliver_signal(current, SIGSEGV_, info);
        }
        // If ptr != NULL, page was mapped/grown successfully - execution will retry

    } // close INT_GPF block
    break;

    default:
        ixland_guest_trace_emit_int(IXLAND_INSTRUMENTATION_ORIGIN_TASK,
                                    "guest.handle_interrupt.unknown", "interrupt", interrupt);
        break;
    }

    // Process any pending signals (e.g., SIGSEGV from page fault)
    trace_handle_interrupt_checkpoint("task.proof.handle_interrupt.before_return", interrupt, 0);
    trace_handle_interrupt_checkpoint("task.proof.handle_interrupt.before_receive_signals",
                                      interrupt, 0);
    receive_signals();
    trace_handle_interrupt_checkpoint("task.proof.handle_interrupt.after_receive_signals",
                                      interrupt, 0);
    trace_handle_interrupt_checkpoint("task.proof.handle_interrupt.return_path_main", interrupt, 0);
}

void a64_dump_syscall(struct cpu_state *cpu)
{
    if (!cpu)
        return;

    char num_buf[32];
    char x0_buf[32];
    char x1_buf[32];
    char x2_buf[32];
    char x3_buf[32];
    char x4_buf[32];
    char x5_buf[32];
    snprintf(num_buf, sizeof(num_buf), "%llu", (unsigned long long)cpu->x[8]);
    snprintf(x0_buf, sizeof(x0_buf), "0x%llx", (unsigned long long)cpu->x[0]);
    snprintf(x1_buf, sizeof(x1_buf), "0x%llx", (unsigned long long)cpu->x[1]);
    snprintf(x2_buf, sizeof(x2_buf), "0x%llx", (unsigned long long)cpu->x[2]);
    snprintf(x3_buf, sizeof(x3_buf), "0x%llx", (unsigned long long)cpu->x[3]);
    snprintf(x4_buf, sizeof(x4_buf), "0x%llx", (unsigned long long)cpu->x[4]);
    snprintf(x5_buf, sizeof(x5_buf), "0x%llx", (unsigned long long)cpu->x[5]);

    ixland_instrumentation_attribute_t attrs[] = {
        { .key = "syscall", .value = num_buf }, { .key = "x0", .value = x0_buf },
        { .key = "x1", .value = x1_buf },       { .key = "x2", .value = x2_buf },
        { .key = "x3", .value = x3_buf },       { .key = "x4", .value = x4_buf },
        { .key = "x5", .value = x5_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_TASK, "guest.syscall.dump", attrs,
                                  sizeof(attrs) / sizeof(attrs[0]));
}
