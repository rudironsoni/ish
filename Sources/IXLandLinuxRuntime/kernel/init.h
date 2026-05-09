#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#import <IXLandLinuxRuntime/fs/tty.h>

// Incredibly sloppy. Please do not reference as an example of good API design.
int mount_root(const struct fs_ops *fs, const char *source);
void set_console_device(int major, int minor);
int become_first_process(void);
int become_new_init_child(void);
int create_stdio(const char *file, int major, int minor);
int create_piped_stdio(void);
int prepare_session_with_tty(const char *exe, const char *const *argv, const char *envp,
                             struct tty *tty, int *pid_out);
void start_prepared_session(void);

#endif
