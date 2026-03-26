/*
 * trace_ring.c
 * Ring buffer flight recorder backend implementation.
 */

#include "trace/trace.h"
#include "trace/trace_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Ring buffer context structure */
typedef struct ring_ctx {
    trace_ring_t ring;
    trace_config_t *config;
} ring_ctx_t;

/* Initialize ring buffer */
static int ring_init(void **ctx, trace_config_t *config) {
    ring_ctx_t *rctx = calloc(1, sizeof(ring_ctx_t));
    if (!rctx) {
        return -1;
    }
    
    /* Allocate ring buffer records */
    size_t capacity = config->ring_size;
    if (capacity < 1024) {
        capacity = 1024;  /* Minimum size */
    }
    if (capacity > 1048576) {
        capacity = 1048576;  /* Maximum size (1M records) */
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
static void ring_shutdown(void *ctx) {
    ring_ctx_t *rctx = (ring_ctx_t *)ctx;
    if (!rctx) return;
    
    if (rctx->ring.records) {
        free(rctx->ring.records);
    }
    free(rctx);
}

/* Emit record to ring buffer */
static void ring_emit(void *ctx, trace_record_t *record) {
    ring_ctx_t *rctx = (ring_ctx_t *)ctx;
    if (!rctx || !rctx->ring.records) return;
    
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
        fprintf(stderr, "[RING-EMIT] SET wrapped=true, head=%zu, capacity=%zu\n", 
                rctx->ring.head, rctx->ring.capacity);
    }
}

/* Flush (no-op for ring buffer) */
static void ring_flush(void *ctx) {
    (void)ctx;
    /* Ring buffer is always "flushed" - records are written immediately */
}

/* Write dump header to file */
static int write_dump_header(FILE *fp, ring_ctx_t *rctx, uint64_t record_count) {
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
static int ring_dump(void *ctx, const char *path) {
    ring_ctx_t *rctx = (ring_ctx_t *)ctx;
    if (!rctx || !rctx->ring.records) return -1;
    
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
    fprintf(stderr, "[TRACE] Dumped %llu records to %s\n", 
            (unsigned long long)count, path);
    
    return 0;
}

/* Dump ring buffer to stderr (human-readable) */
void trace_dump_ring_stderr(void) {
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
        fprintf(stderr, "(showing last 50 of %llu records)\n", 
                (unsigned long long)print_count);
    }
    
    /* Print records */
    for (uint64_t i = start_idx; i < end_idx; i++) {
        size_t ring_idx = i % ring->capacity;
        trace_record_t *rec = &ring->records[ring_idx];
        
        fprintf(stderr, "[%8llu] %20s PC=0x%016llx",
                (unsigned long long)i,
                trace_event_name(rec->header.event_id),
                (unsigned long long)rec->header.pc);
        
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
const trace_backend_ops_t* trace_ring_backend_get_ops(void) {
    return &ring_ops;
}
