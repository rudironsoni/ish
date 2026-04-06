/*
 * trace_ring.c
 * Ring buffer flight recorder backend implementation.
 */

#include "trace.h"
#include "trace_types.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Ring buffer context structure */
typedef struct ring_ctx {
    trace_ring_t ring;
    trace_config_t *config;
} ring_ctx_t;

/* Initialize ring buffer */
static int ring_init(void **ctx, trace_config_t *config)
{
    ring_ctx_t *rctx = calloc(1, sizeof(ring_ctx_t));
    if (!rctx) {
        return -1;
    }

    /* Allocate ring buffer records */
    size_t capacity = config->ring_size;
    if (capacity < 1024) {
        capacity = 1024; /* Minimum size */
    }
    if (capacity > 1048576) {
        capacity = 1048576; /* Maximum size (1M records) */
    }

    rctx->ring.records = calloc(capacity, sizeof(trace_record_t));
    if (!rctx->ring.records) {
        free(rctx);
        return -1;
    }

    rctx->ring.capacity = capacity;
    rctx->ring.head = 0;
    rctx->ring.seq = 0;
    rctx->ring.dropped = 0;
    rctx->ring.wrapped = false;
    rctx->config = config;

    fprintf(stderr, "[RING-INIT] capacity=%zu\n", capacity);

    *ctx = rctx;
    return 0;
}

/* Shutdown ring buffer */
static void ring_shutdown(void *ctx)
{
    ring_ctx_t *rctx = (ring_ctx_t *)ctx;
    if (!rctx)
        return;

    if (rctx->ring.records) {
        free(rctx->ring.records);
    }
    free(rctx);
}

/* Emit record to ring buffer */
static void ring_emit(void *ctx, trace_record_t *record)
{
    ring_ctx_t *rctx = (ring_ctx_t *)ctx;
    if (!rctx || !rctx->ring.records)
        return;

    /* Calculate write position */
    size_t idx = rctx->ring.head % rctx->ring.capacity;

    /* Copy record to ring */
    memcpy(&rctx->ring.records[idx], record, sizeof(trace_record_t));

    /* Update sequence if this is first time at this position */
    if (rctx->ring.head < rctx->ring.capacity) {
        /* First fill - no overwrite yet */
    } else {
        /* Overwrite - count as dropped from perspective of oldest data */
        rctx->ring.dropped++;
    }

    /* Advance head and seq */
    rctx->ring.head++;
    rctx->ring.seq++;

    /* Mark as wrapped if we've filled buffer */
    if (rctx->ring.head >= rctx->ring.capacity) {
        rctx->ring.wrapped = true;
        fprintf(stderr, "[RING-EMIT] SET wrapped=true, head=%zu, capacity=%zu\n", rctx->ring.head,
                rctx->ring.capacity);
    }
}

/* Flush (no-op for ring buffer) */
static void ring_flush(void *ctx)
{
    (void)ctx;
    /* Ring buffer is always "flushed" - records are written immediately */
}

/* Write dump header to file */
static int write_dump_header(FILE *fp, ring_ctx_t *rctx, uint64_t record_count)
{
    trace_dump_header_t header;
    memset(&header, 0, sizeof(header));

    uint8_t magic[] = TRACE_MAGIC;
    memcpy(header.magic, magic, 4);
    header.version = TRACE_VERSION;
    header.header_size = sizeof(trace_dump_header_t);
    header.endianness = TRACE_ENDIAN_LITTLE;
    header.record_header_size = sizeof(trace_record_header_t);
    header.max_payload_size = TRACE_MAX_PAYLOAD;
    header.record_count = record_count;
    header.dropped_count = rctx->ring.dropped;

    /* Calculate start sequence */
    if (rctx->ring.wrapped) {
        header.start_seq = rctx->ring.seq - rctx->ring.capacity;
    } else {
        header.start_seq = 0;
    }

    size_t written = fwrite(&header, sizeof(header), 1, fp);
    return (written == 1) ? 0 : -1;
}

/* Dump ring buffer to file */
static int ring_dump(void *ctx, const char *path)
{
    ring_ctx_t *rctx = (ring_ctx_t *)ctx;
    if (!rctx || !rctx->ring.records)
        return -1;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[TRACE] Failed to open dump file: %s\n", path);
        return -1;
    }

    /* Calculate range to dump */
    uint64_t start_idx, end_idx, count;
    if (rctx->ring.wrapped) {
        /* Buffer has wrapped - dump last N records */
        start_idx = rctx->ring.head - rctx->ring.capacity;
        end_idx = rctx->ring.head;
        count = rctx->ring.capacity;
    } else {
        /* Buffer not yet full - dump what we have */
        start_idx = 0;
        end_idx = rctx->ring.head;
        count = rctx->ring.head;
    }

    /* Write header */
    if (write_dump_header(fp, rctx, count) != 0) {
        fclose(fp);
        return -1;
    }

    /* Write records in order */
    for (uint64_t i = start_idx; i < end_idx; i++) {
        size_t ring_idx = i % rctx->ring.capacity;
        trace_record_t *record = &rctx->ring.records[ring_idx];

        /* Update sequence number in header to be absolute */
        trace_record_t temp_record = *record;
        temp_record.header.seq = i;

        size_t written = fwrite(&temp_record, sizeof(trace_record_t), 1, fp);
        if (written != 1) {
            fprintf(stderr, "[TRACE] Failed to write record to dump file\n");
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    fprintf(stderr, "[TRACE] Dumped %llu records to %s\n", (unsigned long long)count, path);

    return 0;
}

/* Dump ring buffer to stderr (human-readable) */
void trace_dump_ring_stderr(void)
{
    trace_ctx_t *g_ctx = trace_get_global();
    if (!g_ctx || !g_ctx->ring) {
        fprintf(stderr, "[TRACE] No ring buffer available\n");
        return;
    }

    trace_ring_t *ring = g_ctx->ring;

    fprintf(stderr, "\n[TRACE] === Ring Buffer Dump ===\n");
    fprintf(stderr, "Total capacity: %zu\n", ring->capacity);
    fprintf(stderr, "Records: %llu\n", (unsigned long long)ring->seq);
    fprintf(stderr, "Dropped: %llu\n", (unsigned long long)ring->dropped);
    fprintf(stderr, "Wrapped: %s\n", ring->wrapped ? "yes" : "no");
    fprintf(stderr, "Head: %zu\n", ring->head);
    fprintf(stderr, "\n");

    /* Calculate range to print */
    uint64_t start_idx, end_idx;
    if (ring->wrapped) {
        start_idx = ring->seq - ring->capacity;
        end_idx = ring->seq;
    } else {
        start_idx = 0;
        end_idx = ring->seq;
    }

    /* Print last 50 records at most */
    uint64_t print_count = end_idx - start_idx;
    if (print_count > 50) {
        start_idx = end_idx - 50;
        fprintf(stderr, "(showing last 50 of %llu records)\n", (unsigned long long)print_count);
    }

    /* Print records */
    for (uint64_t i = start_idx; i < end_idx; i++) {
        size_t ring_idx = i % ring->capacity;
        trace_record_t *rec = &ring->records[ring_idx];

        fprintf(stderr, "[%8llu] %20s PC=0x%016llx", (unsigned long long)i,
                trace_event_name(rec->header.event_id), (unsigned long long)rec->header.pc);

        /* Add brief payload info */
        switch (rec->header.event_id) {
        case TRACE_EVENT_FAULT: {
            uint64_t fault_addr;
            memcpy(&fault_addr, rec->payload, 8);
            fprintf(stderr, " FAULT_ADDR=0x%llx", (unsigned long long)fault_addr);
            break;
        }
        case TRACE_EVENT_BLOCK_EXIT: {
            int reason;
            memcpy(&reason, rec->payload, 4);
            fprintf(stderr, " EXIT_REASON=%d", reason);
            break;
        }
        case TRACE_EVENT_SYSCALL_ENTER: {
            uint64_t num;
            memcpy(&num, rec->payload, 8);
            fprintf(stderr, " SYSCALL=%llu", (unsigned long long)num);
            break;
        }
        default:
            break;
        }

        fprintf(stderr, "\n");
    }

    fprintf(stderr, "[TRACE] === End Ring Buffer Dump ===\n\n");
}

/* Ring backend operations */
static const trace_backend_ops_t ring_ops = {
    .init = ring_init,
    .shutdown = ring_shutdown,
    .emit = ring_emit,
    .flush = ring_flush,
    .dump = ring_dump,
};

/* Get ring backend operations */
const trace_backend_ops_t *trace_ring_backend_get_ops(void)
{
    return &ring_ops;
}

/* ============================================
 * Pre-Crash Capture - Crash Signal Handler
 * ============================================ */

/* Global crash dump path - now uses app sandbox Diagnostics/Caches directory */
/* The actual path is set at runtime based on the app container */
static char g_crash_dump_path[1024] = { 0 };

/* Get the current crash dump path */
const char *trace_get_crash_dump_path(void)
{
    return g_crash_dump_path;
}

/* Set the crash dump path to a sandbox-safe location */
void trace_set_crash_dump_path(const char *path)
{
    if (path && *path) {
        strncpy(g_crash_dump_path, path, sizeof(g_crash_dump_path) - 1);
        g_crash_dump_path[sizeof(g_crash_dump_path) - 1] = '\0';
    }
}

/* External reference to ring buffer in trace_backends.c */
extern trace_ring_t *trace_get_global_ring(void);

/* Dump ring buffer to file in human-readable format for crash analysis */
static void trace_ring_dump_to_file(const char *path)
{
    trace_ring_t *ring = trace_get_global_ring();
    if (!ring || !ring->records) {
        /* Try to write error to stderr at least */
        fprintf(stderr, "[CRASH-DUMP] No ring buffer available\n");
        return;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[CRASH-DUMP] Failed to open %s: %m\n", path);
        return;
    }

    /* Write header */
    char header[512];
    int n = snprintf(header, sizeof(header),
                     "=== iSH Crash Ring Buffer Dump ===\n"
                     "Timestamp: %ld\n"
                     "Capacity: %zu\n"
                     "Records: %llu\n"
                     "Dropped: %llu\n"
                     "Wrapped: %s\n"
                     "=====================================\n\n",
                     (long)time(NULL), ring->capacity, (unsigned long long)ring->seq,
                     (unsigned long long)ring->dropped, ring->wrapped ? "yes" : "no");
    write(fd, header, n);

    /* Calculate range to dump */
    uint64_t start_idx, end_idx;
    if (ring->wrapped) {
        start_idx = ring->seq - ring->capacity;
        end_idx = ring->seq;
    } else {
        start_idx = 0;
        end_idx = ring->seq;
    }

    /* Dump all records, with TCTI events highlighted */
    uint64_t count = 0;
    for (uint64_t i = start_idx; i < end_idx; i++) {
        size_t ring_idx = i % ring->capacity;
        trace_record_t *rec = &ring->records[ring_idx];

        const char *event_name = trace_event_name(rec->header.event_id);

        /* Format record line */
        char line[256];
        int line_len =
            snprintf(line, sizeof(line), "[%8llu] %20s PC=0x%016llx", (unsigned long long)i,
                     event_name, (unsigned long long)rec->header.pc);

        /* Add payload details for TCTI events */
        switch (rec->header.event_id) {
        case TRACE_EVENT_TCTI_ENTRY_X28_BEFORE: {
            uint64_t val;
            memcpy(&val, rec->payload, 8);
            line_len += snprintf(line + line_len, sizeof(line) - line_len, " x28_before=0x%llx",
                                 (unsigned long long)val);
            break;
        }
        case TRACE_EVENT_TCTI_ENTRY_QWORD0: {
            uint64_t val;
            memcpy(&val, rec->payload, 8);
            line_len += snprintf(line + line_len, sizeof(line) - line_len, " qword0=0x%llx",
                                 (unsigned long long)val);
            break;
        }
        case TRACE_EVENT_TCTI_ENTRY_X27_AFTER: {
            uint64_t val;
            memcpy(&val, rec->payload, 8);
            line_len += snprintf(line + line_len, sizeof(line) - line_len, " x27_after=0x%llx",
                                 (unsigned long long)val);
            break;
        }
        case TRACE_EVENT_TCTI_ENTRY_X28_AFTER: {
            uint64_t val;
            memcpy(&val, rec->payload, 8);
            line_len += snprintf(line + line_len, sizeof(line) - line_len, " x28_after=0x%llx",
                                 (unsigned long long)val);
            break;
        }
        case TRACE_EVENT_TCTI_ENTRY_QWORD1: {
            uint64_t val;
            memcpy(&val, rec->payload, 8);
            line_len += snprintf(line + line_len, sizeof(line) - line_len, " qword1=0x%llx",
                                 (unsigned long long)val);
            break;
        }
        case TRACE_EVENT_GADGET_ENTRY_X28: {
            uint64_t val;
            memcpy(&val, rec->payload, 8);
            line_len += snprintf(line + line_len, sizeof(line) - line_len, " x28_at_entry=0x%llx",
                                 (unsigned long long)val);
            break;
        }
        case TRACE_EVENT_FAULT: {
            uint64_t fault_addr;
            memcpy(&fault_addr, rec->payload, 8);
            line_len += snprintf(line + line_len, sizeof(line) - line_len, " FAULT_ADDR=0x%llx",
                                 (unsigned long long)fault_addr);
            break;
        }
        default:
            break;
        }

        line_len += snprintf(line + line_len, sizeof(line) - line_len, "\n");
        write(fd, line, line_len);
        count++;
    }

    /* Write trailer */
    char trailer[128];
    int tlen = snprintf(trailer, sizeof(trailer), "\n=== End Ring Buffer Dump (%llu records) ===\n",
                        (unsigned long long)count);
    write(fd, trailer, tlen);

    close(fd);

    /* Also log to stderr */
    fprintf(stderr, "[CRASH-DUMP] Wrote %llu records to %s\n", (unsigned long long)count, path);
}

/* Signal handler for crash signals */
static void crash_signal_handler(int sig)
{
    /* Restore default handler to prevent infinite loop if dump fails */
    signal(sig, SIG_DFL);

    const char *sig_name = "UNKNOWN";
    switch (sig) {
    case SIGSEGV:
        sig_name = "SIGSEGV";
        break;
    case SIGBUS:
        sig_name = "SIGBUS";
        break;
    case SIGILL:
        sig_name = "SIGILL";
        break;
    case SIGABRT:
        sig_name = "SIGABRT";
        break;
    case SIGFPE:
        sig_name = "SIGFPE";
        break;
    case SIGTRAP:
        sig_name = "SIGTRAP";
        break;
    }

    /* Write directly to stderr with async-signal-safe functions */
    const char msg[] = "\n[CRASH] Caught signal ";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    write(STDERR_FILENO, sig_name, strlen(sig_name));
    const char msg2[] = " - dumping ring buffer to ";
    write(STDERR_FILENO, msg2, sizeof(msg2) - 1);
    write(STDERR_FILENO, g_crash_dump_path, strlen(g_crash_dump_path));
    const char msg3[] = "\n";
    write(STDERR_FILENO, msg3, sizeof(msg3) - 1);

    /* Dump the ring buffer */
    trace_ring_dump_to_file(g_crash_dump_path);

    /* Re-raise the signal to get default behavior (crash report) */
    raise(sig);
}

/* Install crash signal handlers */
static void install_crash_handlers(void)
{
    static int handlers_installed = 0;
    if (handlers_installed)
        return;

    signal(SIGSEGV, crash_signal_handler);
    signal(SIGBUS, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGTRAP, crash_signal_handler);

    handlers_installed = 1;
    fprintf(stderr, "[CRASH-HANDLER] Signal handlers installed for crash capture\n");
}

/* atexit handler to dump ring buffer on normal exit */
/* Uses sandbox-safe path set at app startup */
static void atexit_dump_ring(void)
{
    const char *exit_path = trace_get_crash_dump_path();
    if (exit_path && *exit_path) {
        trace_ring_dump_to_file(exit_path);
    }
}

/* ============================================
 * Public API: Enable Pre-Crash Capture
 * ============================================ */

void trace_ring_enable_precrash_capture(void)
{
    /* Ensure ring buffer is initialized */
    trace_get_global_ring();

    /* Install crash signal handlers */
    install_crash_handlers();

    /* Register atexit handler for normal exit */
    atexit(atexit_dump_ring);

    fprintf(stderr, "[PRECAPTURE] Pre-crash ring buffer capture enabled\n");
}

/* Dump ring buffer on demand */
void trace_ring_dump_on_crash(void)
{
    trace_ring_dump_to_file(g_crash_dump_path);
}
