/*
 * aarch64 syscall dispatch
 *
 * Handles SVC instructions by dispatching to the appropriate syscall handler
 * using the aarch64 Linux syscall ABI:
 *   x8  = syscall number
 *   x0-x5 = arguments
 *   x0  = return value
 */

#include "kernel/aarch64/calls.h"
#include "kernel/calls.h"
#include "emu/aarch64/cpu.h"
#include "emu/interrupt.h"
#include <string.h>

// Forward declarations for syscall handlers
extern syscall_t syscall_table[];
extern syscall_t syscall_table_a64[A64_SYS_MAX];

// Number of arguments for each syscall (for proper argument marshalling)
// This mirrors the x86 syscall_arg_count in kernel/calls.c
static const uint8_t syscall_arg_count_a64[] = {
    [0 ... A64_SYS_MAX] = 6,  // Default: 6 args
    // Override for specific syscalls
    [A64_SYS_exit] = 1,
    [A64_SYS_exit_group] = 1,
    [A64_SYS_read] = 3,
    [A64_SYS_write] = 3,
    [A64_SYS_openat] = 4,
    [A64_SYS_close] = 1,
    [A64_SYS_brk] = 1,
    [A64_SYS_getpid] = 0,
    [A64_SYS_getppid] = 0,
    [A64_SYS_getuid] = 0,
    [A64_SYS_getgid] = 0,
    [A64_SYS_geteuid] = 0,
    [A64_SYS_getegid] = 0,
};

/*
 * Handle an aarch64 syscall (SVC #0)
 * Called from the execution loop when SVC is encountered
 */
void a64_handle_syscall(struct cpu_state *cpu) {
    // aarch64 syscall ABI:
    // x8 = syscall number
    // x0-x5 = arguments
    // x0 = return value (overwritten)
    // C flag in PSTATE = error indicator

    uint64_t syscall_num = cpu->x[8];

    // Validate syscall number
    if (syscall_num >= A64_SYS_MAX || syscall_table_a64[syscall_num] == NULL) {
        // Unknown syscall - return ENOSYS
        cpu->x[0] = -ENOSYS;
        cpu->c = 1;  // Set carry flag to indicate error
        return;
    }

    syscall_t handler = syscall_table_a64[syscall_num];
    int num_args = syscall_arg_count_a64[syscall_num];

    // Extract arguments based on count
    // Note: For simplicity, we call with 6 args always
    // The handler will use only what it needs
    uint64_t args[6] = {
        cpu->x[0],
        cpu->x[1],
        cpu->x[2],
        cpu->x[3],
        cpu->x[4],
        cpu->x[5]
    };

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

    // Advance PC past the SVC instruction
    cpu->pc += 4;
}

/*
 * Initialize syscall dispatch tables
 * Called once during system initialization
 */
void a64_syscall_init(void) {
    // Validate that all expected syscalls are mapped
    // This helps catch mismatches between syscall numbers and handlers

    // Critical syscalls that must be present
    static const uint64_t critical_syscalls[] = {
        A64_SYS_exit,
        A64_SYS_exit_group,
        A64_SYS_read,
        A64_SYS_write,
        A64_SYS_openat,
        A64_SYS_close,
        A64_SYS_brk,
        A64_SYS_mmap,
        A64_SYS_munmap,
    };

    int missing = 0;
    for (size_t i = 0; i < sizeof(critical_syscalls)/sizeof(critical_syscalls[0]); i++) {
        uint64_t num = critical_syscalls[i];
        if (num >= A64_SYS_MAX || syscall_table_a64[num] == NULL) {
            printk("WARNING: Critical syscall %llu not mapped\n", num);
            missing++;
        }
    }

    if (missing > 0) {
        printk("WARNING: %d critical syscalls missing - some programs may fail\n", missing);
    }
}

/*
 * Get syscall name for debugging
 */
const char *a64_syscall_name(int num) {
    if (num < 0 || num >= A64_SYS_MAX)
        return "unknown";

    static const char *syscall_names[] = {
        [A64_SYS_read] = "read",
        [A64_SYS_write] = "write",
        [A64_SYS_openat] = "openat",
        [A64_SYS_close] = "close",
        [A64_SYS_exit] = "exit",
        [A64_SYS_exit_group] = "exit_group",
        [A64_SYS_brk] = "brk",
        [A64_SYS_mmap] = "mmap",
        [A64_SYS_munmap] = "munmap",
        [A64_SYS_getpid] = "getpid",
        [A64_SYS_getppid] = "getppid",
        [A64_SYS_getuid] = "getuid",
        [A64_SYS_getgid] = "getgid",
    };

    if (num < sizeof(syscall_names)/sizeof(syscall_names[0]) && syscall_names[num])
        return syscall_names[num];

    return "unknown";
}

/*
 * Dump syscall arguments for debugging
 */

// AArch64 interrupt handling
// INT_SYSCALL (128) - Syscall via SVC instruction
// INT_GPF (13)      - General protection fault
void handle_interrupt(int interrupt) {
    struct cpu_state *cpu = &current->cpu;
    
    switch (interrupt) {
        case INT_SYSCALL:
            // Syscalls are dispatched via a64_do_syscall
            // This path is reached when SVC triggers an exception
            // The actual syscall dispatch happens in the TCTI exit path
            break;
            
        case INT_GPF:
            // General protection fault - deliver SIGILL
            deliver_signal(current, SIGILL_, (struct siginfo_) {0});
            break;
            
        default:
            printk("Unknown interrupt %d\n", interrupt);
            break;
    }
}

void a64_dump_syscall(struct cpu_state *cpu) {
    uint64_t num = cpu->x[8];
    const char *name = a64_syscall_name(num);

    printk("SVC #%llu (%s) args: x0=%llx x1=%llx x2=%llx x3=%llx x4=%llx x5=%llx\n",
           num, name,
           cpu->x[0], cpu->x[1], cpu->x[2],
           cpu->x[3], cpu->x[4], cpu->x[5]);
}
