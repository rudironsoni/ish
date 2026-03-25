#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "misc.h"
#include "kernel/calls.h"
#include "kernel/task.h"
#include "emu/aarch64/cpu.h"
#include "xX_main_Xx.h"

// Global pointer to current CPU for crash diagnostics
struct cpu_state *g_current_cpu = NULL;

// Async-signal-safe write helper
static void write_str(int fd, const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    write(fd, s, len);
}

static void write_hex(int fd, unsigned long long val) {
    char buf[32];
    char *p = buf + 31;
    *p = '\n';
    p--;
    if (val == 0) {
        *p = '0';
        p--;
    } else {
        while (val > 0 && p >= buf) {
            int digit = val & 0xF;
            *p = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            val >>= 4;
            p--;
        }
        *p = 'x';
        p--;
        *p = '0';
    }
    write(fd, p, (buf + 31) - p + 1);
}

static void crash_handler(int sig) {
    const char *sig_name = (sig == SIGBUS) ? "SIGBUS" : (sig == SIGSEGV) ? "SIGSEGV" : "UNKNOWN";
    
    write_str(2, "\n=== CRASH DUMP ===\n");
    write_str(2, "Signal: ");
    write_str(2, sig_name);
    write_str(2, "\n");
    
    if (g_current_cpu) {
        write_str(2, "Guest cpu->pc: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->pc);
        
        write_str(2, "Guest sp: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->sp);
        
        write_str(2, "Guest fault_addr: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->fault_addr);
        
        write_str(2, "Guest fault_was_write: ");
        write_hex(2, (unsigned long long)g_current_cpu->fault_was_write);
        
        write_str(2, "Guest x0: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[0]);
        write_str(2, "Guest x1: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[1]);
        write_str(2, "Guest x2: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[2]);
        write_str(2, "Guest x3: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[3]);
        write_str(2, "Guest x4: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[4]);
        write_str(2, "Guest x5: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[5]);
        write_str(2, "Guest x6: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[6]);
        write_str(2, "Guest x7: 0x");
        write_hex(2, (unsigned long long)g_current_cpu->x[7]);
    } else {
        write_str(2, "No current CPU state available\n");
    }
    
    write_str(2, "=== END CRASH DUMP ===\n\n");
    
    _exit(139);
}

int main(int argc, char *const argv[]) {
    // Crash handler DISABLED - was changing behavior
    // signal(SIGBUS, crash_handler);
    // signal(SIGSEGV, crash_handler);
    
    printk("[main] ENTRY\n");
    char envp[100] = {0};
    if (getenv("TERM"))
        strcpy(envp, getenv("TERM") - strlen("TERM") - 1);
    printk("[main] Calling xX_main_Xx\n");
    int err = xX_main_Xx(argc, argv, envp);
    printk("[main] xX_main_Xx returned %d\n", err);
    if (err < 0) {
        fprintf(stderr, "xX_main_Xx: %s\n", strerror(-err));
        return err;
    }
    printk("[main] About to mount procfs\n");
    do_mount(&procfs, "proc", "/proc", "", 0);
    printk("[main] About to mount devptsfs\n");
    do_mount(&devptsfs, "devpts", "/dev/pts", "", 0);
    printk("[main] About to call task_run_current\n");
    task_run_current();
    printk("[main] task_run_current returned (should never happen)\n");
}
