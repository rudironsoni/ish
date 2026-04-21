#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/util/fifo.h>
#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/util/sync.h>
#include <fcntl.h>
#include <ixland/log_bridge.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_BUF_SHIFT 20
static char log_buffer[1 << LOG_BUF_SHIFT];
static struct fifo log_buf = FIFO_INIT(log_buffer);
static size_t log_max_since_clear = 0;
static lock_t log_lock = LOCK_INITIALIZER;

static enum log_level log_level_for_message(const char *msg)
{
    if (!msg)
        return LOG_DEFAULT;
    if (strstr(msg, "FATAL") || strstr(msg, "CRASH") || strstr(msg, "fault:") ||
        strstr(msg, "Assertion failed"))
        return LOG_FAULT;
    if (strstr(msg, "ERROR") || strstr(msg, "error:") || strstr(msg, "failed") ||
        strstr(msg, "Failed"))
        return LOG_ERROR;
    if (strstr(msg, "WARNING") || strstr(msg, "warn:"))
        return LOG_DEFAULT;
    if (strstr(msg, "DEBUG") || strstr(msg, "debug:"))
        return LOG_DEBUG;
    if (strstr(msg, "ENTRY") || strstr(msg, "EXIT") || strstr(msg, "ready") ||
        strstr(msg, "complete"))
        return LOG_INFO;
    return LOG_DEFAULT;
}

static const char *log_category_for_message(const char *msg)
{
    if (!msg)
        return "kernel";
    if (strstr(msg, "FATAL") || strstr(msg, "CRASH"))
        return "crash";
    return "kernel";
}

#define SYSLOG_ACTION_CLOSE_         0
#define SYSLOG_ACTION_OPEN_          1
#define SYSLOG_ACTION_READ_          2
#define SYSLOG_ACTION_READ_ALL_      3
#define SYSLOG_ACTION_READ_CLEAR_    4
#define SYSLOG_ACTION_CLEAR_         5
#define SYSLOG_ACTION_CONSOLE_OFF_   6
#define SYSLOG_ACTION_CONSOLE_ON_    7
#define SYSLOG_ACTION_CONSOLE_LEVEL_ 8
#define SYSLOG_ACTION_SIZE_UNREAD_   9
#define SYSLOG_ACTION_SIZE_BUFFER_   10

static int syslog_read(addr_t buf_addr, int64_t len, int flags)
{
    if (len < 0)
        return _EINVAL;
    if (flags & FIFO_LAST) {
        if ((size_t)len > log_max_since_clear)
            len = log_max_since_clear;
    } else {
        if ((size_t)len > fifo_capacity(&log_buf))
            len = fifo_capacity(&log_buf);
    }
    char *buf = malloc(len);
    fifo_read(&log_buf, buf, len, flags);
    int fail = user_write(buf_addr, buf, len);
    free(buf);
    if (fail)
        return _EFAULT;
    return (int)len;
}

static int do_syslog(int type, addr_t buf_addr, int64_t len)
{
    int res;
    switch (type) {
    case SYSLOG_ACTION_READ_:
        return syslog_read(buf_addr, len, 0);
    case SYSLOG_ACTION_READ_ALL_:
        return syslog_read(buf_addr, len, FIFO_LAST | FIFO_PEEK);

    case SYSLOG_ACTION_READ_CLEAR_:
        res = syslog_read(buf_addr, len, FIFO_LAST | FIFO_PEEK);
        if (res < 0)
            return res;
        ish_fallthrough;
    case SYSLOG_ACTION_CLEAR_:
        log_max_since_clear = 0;
        return 0;

    case SYSLOG_ACTION_SIZE_UNREAD_:
        return (int)fifo_size(&log_buf);
    case SYSLOG_ACTION_SIZE_BUFFER_:
        return (int)fifo_capacity(&log_buf);

    case SYSLOG_ACTION_CLOSE_:
    case SYSLOG_ACTION_OPEN_:
    case SYSLOG_ACTION_CONSOLE_OFF_:
    case SYSLOG_ACTION_CONSOLE_ON_:
    case SYSLOG_ACTION_CONSOLE_LEVEL_:
        return 0;
    default:
        return _EINVAL;
    }
}
int64_t sys_syslog(int64_t type, addr_t buf_addr, int64_t len)
{
    lock(&log_lock);
    int retval = do_syslog((int)type, buf_addr, len);
    unlock(&log_lock);
    return retval;
}

static void log_buf_append(const char *msg)
{
    fifo_write(&log_buf, msg, strlen(msg), FIFO_OVERWRITE);
    log_max_since_clear += strlen(msg);
    if (log_max_since_clear > fifo_capacity(&log_buf))
        log_max_since_clear = fifo_capacity(&log_buf);
}

static void log_line(const char *line)
{
    enum log_level level = log_level_for_message(line);
    const char *category = log_category_for_message(line);
    log_emit_impl(level, "app.ixland.terminal", category, line);
}

static void output_line(const char *line)
{
    log_line(line);
    log_buf_append(line);
    log_buf_append("\n");
}

void ish_vprintk(const char *msg, va_list args)
{
    static __thread char buf[16384] = "";
    static __thread size_t buf_size = 0;
    buf_size += vsprintf(buf + buf_size, msg, args);

    lock(&log_lock);
    char *b = buf;
    char *p;
    while ((p = strchr(b, '\n')) != NULL) {
        *p = '\0';
        output_line(b);
        *p = '\n';
        buf_size -= p + 1 - b;
        b = p + 1;
    }
    unlock(&log_lock);
    memmove(buf, b, strlen(b) + 1);
}
void ish_printk(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    ish_vprintk(msg, args);
    va_end(args);
}

static void default_die_handler(const char *msg)
{
    printk("%s\n", msg);
}
void (*die_handler)(const char *msg) = default_die_handler;
_Noreturn void die(const char *msg, ...);
void die(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    char buf[4096];
    vsprintf(buf, msg, args);

    log_fault_fatal_impl(buf);

    extern const char *g_crash_ring_path;
    extern int trace_persist_ring(const char *path);
    if (g_crash_ring_path) {
        trace_persist_ring(g_crash_ring_path);
    }

    die_handler(buf);
    abort();
    va_end(args);
}

int current_pid(void)
{
    if (current)
        return current->pid;
    return -1;
}
