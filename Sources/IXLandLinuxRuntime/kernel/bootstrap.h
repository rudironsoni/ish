#ifndef IXLAND_KERNEL_BOOTSTRAP_H
#define IXLAND_KERNEL_BOOTSTRAP_H

#import <IXLandLinuxRuntime/fs/tty.h>

int runtime_finish_post_mount_setup(void);
int runtime_prepare_console(struct tty_driver *console_driver, int console_minor);

#endif
