#include "pty_host_bridge.h"
#include <stddef.h>

static pty_host_bridge_ops_t g_pty_host_bridge_ops = {};

void pty_host_bridge_install(const pty_host_bridge_ops_t *ops)
{
    g_pty_host_bridge_ops = ops != NULL ? *ops : (pty_host_bridge_ops_t) {};
}

bool pty_host_bridge_can_send_output(void)
{
    return g_pty_host_bridge_ops.send_output != NULL;
}

int pty_host_bridge_send_output(nsobj_t terminal, const char *data, int size)
{
    if (g_pty_host_bridge_ops.send_output == NULL)
        return size;
    return g_pty_host_bridge_ops.send_output(terminal, data, size);
}

void pty_host_bridge_release_bound_tty_data(nsobj_t terminal)
{
    if (g_pty_host_bridge_ops.release_bound_tty_data != NULL)
        g_pty_host_bridge_ops.release_bound_tty_data(terminal);
}
