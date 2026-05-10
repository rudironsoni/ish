#ifndef IXLAND_IOS_PTY_HOST_BRIDGE_H
#define IXLAND_IOS_PTY_HOST_BRIDGE_H

#include <stdbool.h>

#ifndef IXLAND_NSOBJ_T_DEFINED
#define IXLAND_NSOBJ_T_DEFINED
typedef const void *nsobj_t;
#endif

typedef struct pty_host_bridge_ops {
    int (*send_output)(nsobj_t terminal, const char *data, int size);
    void (*release_bound_tty_data)(nsobj_t terminal);
} pty_host_bridge_ops_t;

void pty_host_bridge_install(const pty_host_bridge_ops_t *ops);
bool pty_host_bridge_can_send_output(void);
int pty_host_bridge_send_output(nsobj_t terminal, const char *data, int size);
void pty_host_bridge_release_bound_tty_data(nsobj_t terminal);

#endif
