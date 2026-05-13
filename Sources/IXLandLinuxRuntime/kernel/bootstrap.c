#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/path.h>
#import <IXLandLinuxRuntime/kernel/bootstrap.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#include <sys/stat.h>

static int ensure_char_device_node(const char *path, mode_t_ mode, dev_t_ dev)
{
    struct statbuf stat = {};
    int err = generic_statat(AT_PWD, path, &stat, false);
    if (err == 0) {
        if (S_ISCHR(stat.mode) && stat.rdev == dev) {
            generic_setattrat(AT_PWD, path, (struct attr){ .type = attr_mode, .mode = mode }, false);
            return 0;
        }
        err = generic_unlinkat(AT_PWD, path);
        if (err < 0)
            return err;
    } else if (err != _ENOENT) {
        return err;
    }

    err = generic_mknodat(AT_PWD, path, mode, dev);
    if (err == _EEXIST) {
        err = generic_statat(AT_PWD, path, &stat, false);
        if (err == 0 && S_ISCHR(stat.mode) && stat.rdev == dev)
            return 0;
    }
    return err;
}

static int create_default_device_nodes(void)
{
    int err = 0;
#define ENSURE_DEV_NODE(path, major, minor)                                                   \
    do {                                                                                       \
        err = ensure_char_device_node(path, S_IFCHR | 0666, dev_make(major, minor));         \
        if (err < 0)                                                                           \
            return err;                                                                        \
    } while (0)

    ENSURE_DEV_NODE("/dev/tty1", TTY_CONSOLE_MAJOR, 1);
    ENSURE_DEV_NODE("/dev/tty2", TTY_CONSOLE_MAJOR, 2);
    ENSURE_DEV_NODE("/dev/tty3", TTY_CONSOLE_MAJOR, 3);
    ENSURE_DEV_NODE("/dev/tty4", TTY_CONSOLE_MAJOR, 4);
    ENSURE_DEV_NODE("/dev/tty5", TTY_CONSOLE_MAJOR, 5);
    ENSURE_DEV_NODE("/dev/tty6", TTY_CONSOLE_MAJOR, 6);
    ENSURE_DEV_NODE("/dev/tty7", TTY_CONSOLE_MAJOR, 7);
    ENSURE_DEV_NODE("/dev/tty", TTY_ALTERNATE_MAJOR, DEV_TTY_MINOR);
    ENSURE_DEV_NODE("/dev/console", TTY_ALTERNATE_MAJOR, DEV_CONSOLE_MINOR);
    ENSURE_DEV_NODE("/dev/ptmx", TTY_ALTERNATE_MAJOR, DEV_PTMX_MINOR);
    ENSURE_DEV_NODE("/dev/null", MEM_MAJOR, DEV_NULL_MINOR);
    ENSURE_DEV_NODE("/dev/zero", MEM_MAJOR, DEV_ZERO_MINOR);
    ENSURE_DEV_NODE("/dev/full", MEM_MAJOR, DEV_FULL_MINOR);
    ENSURE_DEV_NODE("/dev/random", MEM_MAJOR, DEV_RANDOM_MINOR);
    ENSURE_DEV_NODE("/dev/urandom", MEM_MAJOR, DEV_URANDOM_MINOR);

    err = generic_mkdirat(AT_PWD, "/dev/pts", 0755);
    if (err < 0 && err != _EEXIST && err != _EBUSY && err != _EPERM)
        return err;
    // Best-effort root mode normalization; some host-backed surfaces may reject it.
    (void)generic_setattrat(AT_PWD, "/", (struct attr) {.type = attr_mode, .mode = 0755}, false);
#undef ENSURE_DEV_NODE
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
