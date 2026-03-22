#include <stdlib.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/task.h"
#include "xX_main_Xx.h"

int main(int argc, char *const argv[]) {
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
