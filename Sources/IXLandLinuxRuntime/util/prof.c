/*
 * iSH Profiling Infrastructure - Implementation
 */

#import <IXLandLinuxRuntime/util/prof.h>

#import <IXLandLinuxRuntime/util/misc.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// Continuous profiling using platform timers
#include <time.h>

// Global profiling state
prof_writer_t g_prof_writer = { 0 };
_Thread_local int prof_thread_id = -1;

// Thread registry
static struct {
    pthread_mutex_t lock;
    int next_id;
    int active_count;
} g_thread_registry = { .lock = PTHREAD_MUTEX_INITIALIZER, .next_id = 0, .active_count = 0 };

// Continuous profiling state
static struct {
    uint32_t interval_ms;
    bool running;
    pthread_mutex_t lock;
} g_continuous = { .running = false, .lock = PTHREAD_MUTEX_INITIALIZER };

// Buffer size for profile output
#define PROF_BUFFER_SIZE (16 * 1024 * 1024) // 16MB buffer

// Get current thread ID (register if needed)
static int get_thread_id(void)
{
    if (prof_thread_id < 0) {
        pthread_mutex_lock(&g_thread_registry.lock);
        prof_thread_id = g_thread_registry.next_id++;
        g_thread_registry.active_count++;
        pthread_mutex_unlock(&g_thread_registry.lock);
    }
    return prof_thread_id;
}

// Get current time in nanoseconds
static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Flush buffer to output file
static void flush_buffer(void)
{
    if (g_prof_writer.buf_used > 0 && g_prof_writer.fd >= 0) {
        ssize_t written = write(g_prof_writer.fd, g_prof_writer.buffer, g_prof_writer.buf_used);
        if (written > 0) {
            g_prof_writer.buf_used = 0;
        }
    }
}

// Write data to buffer (with auto-flush)
static void buffer_write(const char *data, size_t len)
{
    if (!g_prof_writer.enabled || g_prof_writer.fd < 0)
        return;

    // Flush if buffer would overflow
    if (g_prof_writer.buf_used + len > g_prof_writer.buf_size) {
        flush_buffer();
    }

    // If still too large, write directly
    if (len > g_prof_writer.buf_size) {
        write(g_prof_writer.fd, data, len);
        return;
    }

    memcpy(g_prof_writer.buffer + g_prof_writer.buf_used, data, len);
    g_prof_writer.buf_used += len;
}

// Write formatted string to buffer
static void buffer_printf(const char *fmt, ...)
{
    char tmp[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);

    if (len > 0) {
        buffer_write(tmp, (size_t)len);
    }
}

// Write JSON-escaped string
static void buffer_json_string(const char *str)
{
    if (!str) {
        buffer_write("null", 4);
        return;
    }

    buffer_write("\"", 1);
    for (const char *p = str; *p; p++) {
        switch (*p) {
        case '"':
            buffer_write("\\\"", 2);
            break;
        case '\\':
            buffer_write("\\\\", 2);
            break;
        case '\b':
            buffer_write("\\b", 2);
            break;
        case '\f':
            buffer_write("\\f", 2);
            break;
        case '\n':
            buffer_write("\\n", 2);
            break;
        case '\r':
            buffer_write("\\r", 2);
            break;
        case '\t':
            buffer_write("\\t", 2);
            break;
        default:
            if (*p >= 0x20 && *p < 0x7f) {
                buffer_write(p, 1);
            } else {
                buffer_printf("\\u%04x", (unsigned char)*p);
            }
        }
    }
    buffer_write("\"", 1);
}

// Public API implementation

bool prof_init(const char *output_path)
{
    if (g_prof_writer.enabled) {
        return true; // Already initialized
    }

    // Get output path from environment or use default
    if (!output_path) {
        output_path = getenv("ISH_PROFILE_OUTPUT");
    }
    if (!output_path) {
        output_path = "ish-profile.json";
    }

    // Allocate buffer
    g_prof_writer.buffer = malloc(PROF_BUFFER_SIZE);
    if (!g_prof_writer.buffer) {
        fprintf(stderr, "prof: failed to allocate buffer\n");
        return false;
    }
    g_prof_writer.buf_size = PROF_BUFFER_SIZE;
    g_prof_writer.buf_used = 0;

    // Open output file
    g_prof_writer.fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_prof_writer.fd < 0) {
        fprintf(stderr, "prof: failed to open %s: %s\n", output_path, strerror(errno));
        free(g_prof_writer.buffer);
        g_prof_writer.buffer = NULL;
        return false;
    }

    // Write JSON header
    buffer_write("{\n", 2);
    buffer_printf("  \"version\": 1,\n");
    buffer_printf("  \"start_time\": %llu,\n", (unsigned long long)get_time_ns());
    buffer_printf("  \"samples\": [\n");

    g_prof_writer.sample_count = 0;
    g_prof_writer.enabled = true;
    clock_gettime(CLOCK_MONOTONIC, &g_prof_writer.start_time);

    get_thread_id(); // Register main thread

    return true;
}

void prof_shutdown(void)
{
    if (!g_prof_writer.enabled)
        return;

    // Close JSON array and object
    if (g_prof_writer.sample_count > 0) {
        buffer_write("\n", 1); // End last line
    }
    buffer_write("  ],\n", 5);

    // Write summary
    uint64_t duration_ns = prof_get_duration_ns();
    buffer_printf("  \"summary\": {\n");
    buffer_printf("    \"total_samples\": %llu,\n", (unsigned long long)g_prof_writer.sample_count);
    buffer_printf("    \"duration_ns\": %llu,\n", (unsigned long long)duration_ns);
    buffer_printf("    \"duration_ms\": %.2f,\n", duration_ns / 1000000.0);
    buffer_printf("    \"samples_per_second\": %.2f\n",
                  g_prof_writer.sample_count / (duration_ns / 1000000000.0));
    buffer_printf("  }\n");
    buffer_write("}\n", 2);

    flush_buffer();

    if (g_prof_writer.fd >= 0) {
        close(g_prof_writer.fd);
        g_prof_writer.fd = -1;
    }

    free(g_prof_writer.buffer);
    g_prof_writer.buffer = NULL;
    g_prof_writer.enabled = false;
}

void prof_capture_sample(prof_event_type_t type)
{
    if (!g_prof_writer.enabled)
        return;

    prof_sample_t sample = { 0 };
    sample.timestamp_ns = get_time_ns();
    sample.thread_id = get_thread_id();
    sample.type = type;

    // Write sample as JSON
    if (g_prof_writer.sample_count > 0) {
        buffer_write(",\n", 2);
    }
    buffer_write("    {\n", 5);
    buffer_printf("      \"timestamp\": %llu,\n", (unsigned long long)sample.timestamp_ns);
    buffer_printf("      \"thread\": %d,\n", sample.thread_id);
    buffer_printf("      \"type\": %d\n", type);
    buffer_write("    }", 4);

    g_prof_writer.sample_count++;

    // Periodic flush every 1000 samples
    if (g_prof_writer.sample_count % 1000 == 0) {
        flush_buffer();
    }
}

uint64_t prof_begin(prof_event_type_t type)
{
    (void)type;
    return get_time_ns();
}

void prof_end(uint64_t handle, prof_event_type_t type)
{
    if (!g_prof_writer.enabled || handle == 0)
        return;

    uint64_t end_time = get_time_ns();
    uint64_t duration = end_time - handle;

    if (g_prof_writer.sample_count > 0) {
        buffer_write(",\n", 2);
    }
    buffer_write("    {\n", 5);
    buffer_printf("      \"timestamp\": %llu,\n", (unsigned long long)end_time);
    buffer_printf("      \"thread\": %d,\n", get_thread_id());
    buffer_printf("      \"type\": %d,\n", type);
    buffer_printf("      \"duration_ns\": %llu\n", (unsigned long long)duration);
    buffer_write("    }", 4);

    g_prof_writer.sample_count++;
}

void prof_record_alloc(void *ptr, size_t size, const char *name)
{
    if (!g_prof_writer.enabled)
        return;

    if (g_prof_writer.sample_count > 0) {
        buffer_write(",\n", 2);
    }
    buffer_write("    {\n", 5);
    buffer_printf("      \"timestamp\": %llu,\n", (unsigned long long)get_time_ns());
    buffer_printf("      \"thread\": %d,\n", get_thread_id());
    buffer_printf("      \"type\": %d,\n", PROF_EVENT_ALLOC);
    buffer_printf("      \"ptr\": \"%p\",\n", ptr);
    buffer_printf("      \"size\": %zu,\n", size);
    buffer_write("      \"func\": ", 12);
    buffer_json_string(name);
    buffer_write("\n    }", 5);

    g_prof_writer.sample_count++;
}

void prof_record_free(void *ptr)
{
    if (!g_prof_writer.enabled)
        return;

    if (g_prof_writer.sample_count > 0) {
        buffer_write(",\n", 2);
    }
    buffer_write("    {\n", 5);
    buffer_printf("      \"timestamp\": %llu,\n", (unsigned long long)get_time_ns());
    buffer_printf("      \"thread\": %d,\n", get_thread_id());
    buffer_printf("      \"type\": %d,\n", PROF_EVENT_FREE);
    buffer_printf("      \"ptr\": \"%p\"\n", ptr);
    buffer_write("    }", 3);

    g_prof_writer.sample_count++;
}

void prof_record_tlb_miss(uint64_t vaddr, bool is_write, int level)
{
    if (!g_prof_writer.enabled)
        return;

    if (g_prof_writer.sample_count > 0) {
        buffer_write(",\n", 2);
    }
    buffer_write("    {\n", 5);
    buffer_printf("      \"timestamp\": %llu,\n", (unsigned long long)get_time_ns());
    buffer_printf("      \"thread\": %d,\n", get_thread_id());
    buffer_printf("      \"type\": %d,\n", PROF_EVENT_TLB_MISS);
    buffer_printf("      \"vaddr\": \"0x%llx\",\n", (unsigned long long)vaddr);
    buffer_printf("      \"is_write\": %s,\n", is_write ? "true" : "false");
    buffer_printf("      \"level\": %d\n", level);
    buffer_write("    }", 3);

    g_prof_writer.sample_count++;
}

void prof_record_tb_compile(uint64_t guest_ip, uint32_t insn_count, size_t host_code_size)
{
    if (!g_prof_writer.enabled)
        return;

    if (g_prof_writer.sample_count > 0) {
        buffer_write(",\n", 2);
    }
    buffer_write("    {\n", 5);
    buffer_printf("      \"timestamp\": %llu,\n", (unsigned long long)get_time_ns());
    buffer_printf("      \"thread\": %d,\n", get_thread_id());
    buffer_printf("      \"type\": %d,\n", PROF_EVENT_TB_COMPILE);
    buffer_printf("      \"guest_ip\": \"0x%llx\",\n", (unsigned long long)guest_ip);
    buffer_printf("      \"insn_count\": %u,\n", insn_count);
    buffer_printf("      \"code_size\": %zu\n", host_code_size);
    buffer_write("    }", 3);

    g_prof_writer.sample_count++;
}

void prof_record_tb_execute(uint64_t guest_ip, uint64_t cycles)
{
    if (!g_prof_writer.enabled)
        return;

    // Sample only every N executions to reduce overhead
    static _Thread_local uint64_t counter = 0;
    if (++counter % 1000 != 0)
        return;

    if (g_prof_writer.sample_count > 0) {
        buffer_write(",\n", 2);
    }
    buffer_write("    {\n", 5);
    buffer_printf("      \"timestamp\": %llu,\n", (unsigned long long)get_time_ns());
    buffer_printf("      \"thread\": %d,\n", get_thread_id());
    buffer_printf("      \"type\": %d,\n", PROF_EVENT_TB_EXECUTE);
    buffer_printf("      \"guest_ip\": \"0x%llx\",\n", (unsigned long long)guest_ip);
    buffer_printf("      \"cycles\": %llu\n", (unsigned long long)cycles);
    buffer_write("    }", 3);

    g_prof_writer.sample_count++;
}

void prof_dump_stats(void)
{
    uint64_t duration_ns = prof_get_duration_ns();

    fprintf(stderr, "=== iSH Profile Stats ===\n");
    fprintf(stderr, "Total samples: %llu\n", (unsigned long long)g_prof_writer.sample_count);
    fprintf(stderr, "Duration: %.2f ms\n", duration_ns / 1000000.0);
    fprintf(stderr, "Rate: %.2f samples/sec\n",
            g_prof_writer.sample_count / (duration_ns / 1000000000.0));
    fprintf(stderr, "Active threads: %d\n", g_thread_registry.active_count);
    fprintf(stderr, "=======================\n");
}

uint64_t prof_get_sample_count(void)
{
    return g_prof_writer.sample_count;
}

uint64_t prof_get_duration_ns(void)
{
    if (!g_prof_writer.enabled)
        return 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t start_ns = (uint64_t)g_prof_writer.start_time.tv_sec * 1000000000ULL +
                        g_prof_writer.start_time.tv_nsec;
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;

    return now_ns - start_ns;
}

int prof_format_stats_json(char *buf, size_t buf_size)
{
    uint64_t duration_ns = prof_get_duration_ns();

    return snprintf(buf, buf_size,
                    "{\n"
                    "  \"enabled\": %s,\n"
                    "  \"samples\": %llu,\n"
                    "  \"duration_ns\": %llu,\n"
                    "  \"threads\": %d\n"
                    "}\n",
                    g_prof_writer.enabled ? "true" : "false",
                    (unsigned long long)g_prof_writer.sample_count, (unsigned long long)duration_ns,
                    g_thread_registry.active_count);
}

// Scoped profiling
uint64_t prof_begin_scoped(const char *name, const char *file, int line)
{
    (void)name;
    (void)file;
    (void)line;
    return get_time_ns();
}

void prof_scope_cleanup(uint64_t *handle)
{
    if (*handle != 0) {
        prof_end(*handle, PROF_EVENT_SAMPLE);
    }
}

// Flame graph output
bool prof_write_flamegraph(const char *output_path)
{
    if (!g_prof_writer.enabled)
        return false;

    int fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;

    // Write header comment
    const char *header = "# Flame graph data - folded format\n";
    write(fd, header, strlen(header));

    // TODO: Implement actual stack trace collection and folding
    // For now, write placeholder
    const char *placeholder = "prof_placeholder 1\n";
    write(fd, placeholder, strlen(placeholder));

    close(fd);
    return true;
}

// Continuous profiling
bool prof_start_continuous(uint32_t interval_ms)
{
    (void)interval_ms;
    return false; // Continuous profiling not supported on iOS
}
void prof_stop_continuous(void)
{
    pthread_mutex_lock(&g_continuous.lock);
    g_continuous.running = false;
    pthread_mutex_unlock(&g_continuous.lock);
}

// Constructor/destructor for auto-init
#ifdef ENABLE_PROFILING
__attribute__((constructor)) static void prof_auto_init(void)
{
    const char *env = getenv("ISH_PROFILE_AUTO");
    if (env && strcmp(env, "1") == 0) {
        prof_init(NULL);
    }
}

__attribute__((destructor)) static void prof_auto_shutdown(void)
{
    if (g_prof_writer.enabled) {
        prof_shutdown();
    }
}
#endif
