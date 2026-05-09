#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/path.h>
#import <IXLandLinuxRuntime/kernel/bootstrap.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#include <sys/stat.h>

static int create_default_device_nodes(void)
{
    generic_mknodat(AT_PWD, "/dev/tty1", S_IFCHR | 0666, dev_make(TTY_CONSOLE_MAJOR, 1));
    generic_mknodat(AT_PWD, "/dev/tty2", S_IFCHR | 0666, dev_make(TTY_CONSOLE_MAJOR, 2));
    generic_mknodat(AT_PWD, "/dev/tty3", S_IFCHR | 0666, dev_make(TTY_CONSOLE_MAJOR, 3));
    generic_mknodat(AT_PWD, "/dev/tty4", S_IFCHR | 0666, dev_make(TTY_CONSOLE_MAJOR, 4));
    generic_mknodat(AT_PWD, "/dev/tty5", S_IFCHR | 0666, dev_make(TTY_CONSOLE_MAJOR, 5));
    generic_mknodat(AT_PWD, "/dev/tty6", S_IFCHR | 0666, dev_make(TTY_CONSOLE_MAJOR, 6));
    generic_mknodat(AT_PWD, "/dev/tty7", S_IFCHR | 0666, dev_make(TTY_CONSOLE_MAJOR, 7));
    generic_mknodat(AT_PWD, "/dev/tty", S_IFCHR | 0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_TTY_MINOR));
    generic_mknodat(AT_PWD, "/dev/console", S_IFCHR | 0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_CONSOLE_MINOR));
    generic_mknodat(AT_PWD, "/dev/ptmx", S_IFCHR | 0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_PTMX_MINOR));
    generic_mknodat(AT_PWD, "/dev/null", S_IFCHR | 0666, dev_make(MEM_MAJOR, DEV_NULL_MINOR));
    generic_mknodat(AT_PWD, "/dev/zero", S_IFCHR | 0666, dev_make(MEM_MAJOR, DEV_ZERO_MINOR));
    generic_mknodat(AT_PWD, "/dev/full", S_IFCHR | 0666, dev_make(MEM_MAJOR, DEV_FULL_MINOR));
    generic_mknodat(AT_PWD, "/dev/random", S_IFCHR | 0666, dev_make(MEM_MAJOR, DEV_RANDOM_MINOR));
    generic_mknodat(AT_PWD, "/dev/urandom", S_IFCHR | 0666, dev_make(MEM_MAJOR, DEV_URANDOM_MINOR));
    generic_mkdirat(AT_PWD, "/dev/pts", 0755);
    generic_setattrat(AT_PWD, "/", (struct attr) {.type = attr_mode, .mode = 0755}, false);
    return 0;
}

int runtime_finish_post_mount_setup(void)
{
    int err = create_default_device_nodes();
    if (err < 0)
        return err;
    do_mount(&procfs, "proc", "/proc", "", 0);
    do_mount(&devptsfs, "devpts", "/dev/pts", "", 0);
    return 0;
}

int runtime_prepare_console(struct tty_driver *console_driver, int console_minor)
{
    if (console_driver == NULL)
        return -_EINVAL;

    tty_drivers[TTY_CONSOLE_MAJOR] = console_driver;
    set_console_device(TTY_CONSOLE_MAJOR, console_minor);
    return create_stdio("/dev/console", TTY_CONSOLE_MAJOR, console_minor);
}
