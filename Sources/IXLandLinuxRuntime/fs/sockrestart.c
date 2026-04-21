#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/fs/sock.h>
#import <IXLandLinuxRuntime/fs/sockrestart.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/util/list.h>
#include <ixland/sock_bridge.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

extern const struct fd_ops socket_fdops;

static lock_t sockrestart_lock = LOCK_INITIALIZER;
static struct list listen_fds = LIST_INITIALIZER(listen_fds);

void sockrestart_begin_listen(struct fd *sock)
{
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    list_add(&listen_fds, &sock->sockrestart.listen);
    unlock(&sockrestart_lock);
}

void sockrestart_end_listen(struct fd *sock)
{
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    list_remove_safe(&sock->sockrestart.listen);
    unlock(&sockrestart_lock);
}

static struct list listen_tasks = LIST_INITIALIZER(listen_tasks);

void sockrestart_begin_listen_wait(struct fd *sock)
{
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    if (current->sockrestart.count == 0)
        list_add(&listen_tasks, &current->sockrestart.listen);
    current->sockrestart.count++;
    unlock(&sockrestart_lock);
}

void sockrestart_end_listen_wait(struct fd *sock)
{
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    current->sockrestart.count--;
    if (current->sockrestart.count == 0)
        list_remove(&current->sockrestart.listen);
    unlock(&sockrestart_lock);
}

bool sockrestart_should_restart_listen_wait(void)
{
    lock(&sockrestart_lock);
    bool punt = current->sockrestart.punt;
    current->sockrestart.punt = false;
    unlock(&sockrestart_lock);
    return punt;
}

struct saved_socket {
    struct fd *sock;
    int type;
    int proto;
    char name[128];
    uint32_t name_len;
    struct list saved;
};

static struct list saved_sockets = LIST_INITIALIZER(saved_sockets);

void sockrestart_on_suspend(void)
{
    lock(&sockrestart_lock);
    assert(list_empty(&saved_sockets));
    struct fd *sock;
    list_for_each_entry (&listen_fds, sock, sockrestart.listen) {
        struct saved_socket *saved = malloc(sizeof(struct saved_socket));
        if (saved == NULL)
            continue;
        saved->sock = fd_retain(sock);
        saved->proto = sock->socket.protocol;
        int32_t size = sizeof(saved->type);
        getsockopt_impl(sock->real_fd, SOL_SOCKET_, SO_TYPE_, &saved->type, &size);
        saved->name_len = sizeof(saved->name);
        struct sockaddr_result res = getsockname_impl(sock->real_fd, saved->name, saved->name_len);
        if (res.ret >= 0)
            saved->name_len = res.addr_len;
        list_add(&saved_sockets, &saved->saved);
    }
    unlock(&sockrestart_lock);
}

void sockrestart_on_resume(void)
{
    lock(&sockrestart_lock);
    struct saved_socket *saved, *tmp;
    list_for_each_entry_safe(&saved_sockets, saved, tmp, saved)
    {
        list_remove(&saved->saved);
        int real_family = sockaddr_get_family_impl(saved->name);
        int32_t new_sock = socket_impl(real_family, saved->type, saved->proto);
        if (new_sock < 0) {
            printk("restarting socket(%d, %d, %d) failed\n", real_family, saved->type,
                   saved->proto);
            goto thank_u_next;
        }
        int32_t ret = bind_impl(new_sock, saved->name, saved->name_len);
        if (ret < 0) {
            printk("rebinding socket failed\n");
            goto thank_u_next;
        }
        dup2(new_sock, saved->sock->real_fd);

thank_u_next:
        fd_close(saved->sock);
    }
    struct task *task;
    list_for_each_entry (&listen_tasks, task, sockrestart.listen) {
        task->sockrestart.punt = true;
        pthread_kill(task->thread, SIGUSR1);
    }
    unlock(&sockrestart_lock);
}
