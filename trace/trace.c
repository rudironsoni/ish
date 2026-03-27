/*
 * trace.c
 * Core implementation of the iSH tracing subsystem.
 */

#include "trace/trace.h"
#include "trace/trace_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* Global trace context */
trace_ctx_t *g_trace_ctx = NULL;

/* Event descriptor table - populated from trace_events.def */
static const trace_event_desc_t event_descriptors[TRACE_EVENT_MAX] = {
    [TRACE_EVENT_NONE] = { "NONE", TRACE_LEVEL_OFF, 0, 0 },
    
    #define TRACE_EVENT(name, level, category, payload_size) \
        [TRACE_EVENT_##name] = { #name, level, category, payload_size },
    #include "trace/trace_events.def"
    #undef TRACE_EVENT
};

/* Check if event should be emitted */
bool trace_event_enabled(trace_event_id_t event, uint64_t pc) {
    if (!g_trace_ctx || !g_trace_ctx->enabled) {
        return false;
    }
    
    trace_config_t *cfg = &g_trace_ctx->config;
    const trace_event_desc_t *desc = &event_descriptors[event];
    
    /* Check level */
    if (desc->level > cfg->level) {
        return false;
    }
    
    /* Check event mask - if 0, allow all events */
    if (cfg->event_mask != 0 && !(cfg->event_mask & (1ULL << event))) {
        return false;
    }
    
    /* Check category mask */
    if (!(cfg->category_mask & desc->category)) {
        return false;
    }
    
    /* Check PC range filter */
    if (cfg->pc_start != 0 || cfg->pc_end != 0) {
        if (pc < cfg->pc_start || pc > cfg->pc_end) {
            return false;
        }
    }
    
    return true;
}

/* Initialize tracing */
int trace_init(trace_config_t *config) {
    if (g_trace_ctx) {
        /* Already initialized - shutdown first */
        trace_shutdown();
    }
    
    g_trace_ctx = calloc(1, sizeof(trace_ctx_t));
    if (!g_trace_ctx) {
        return -1;
    }
    
    /* Copy configuration */
    memcpy(&g_trace_ctx->config, config, sizeof(trace_config_t));
    
    /* Get backend operations */
    g_trace_ctx->backend_ops = trace_backend_get_ops(config->backend);
    if (!g_trace_ctx->backend_ops) {
        free(g_trace_ctx);
        g_trace_ctx = NULL;
        return -1;
    }
    
    /* Initialize backend */
    if (g_trace_ctx->backend_ops->init) {
        int ret = g_trace_ctx->backend_ops->init(&g_trace_ctx->backend_ctx, config);
        if (ret != 0) {
            free(g_trace_ctx);
            g_trace_ctx = NULL;
            return ret;
        }
    }
    
    /* If ring backend, store ring reference for dumping */
    if (config->backend == TRACE_BACKEND_RING) {
        /* backend_ctx is ring_ctx_t*, ring is inside it */
        typedef struct ring_ctx { trace_ring_t ring; trace_config_t *config; } ring_ctx_t;
        ring_ctx_t *rctx = (ring_ctx_t *)g_trace_ctx->backend_ctx;
        g_trace_ctx->ring = &rctx->ring;
    }
    
    g_trace_ctx->enabled = (config->level > TRACE_LEVEL_OFF);
    
    return 0;
}

/* Shutdown tracing */
void trace_shutdown(void) {
    if (!g_trace_ctx) {
        return;
    }
    
    if (g_trace_ctx->backend_ops && g_trace_ctx->backend_ops->shutdown) {
        g_trace_ctx->backend_ops->shutdown(g_trace_ctx->backend_ctx);
    }
    
    free(g_trace_ctx);
    g_trace_ctx = NULL;
}

/* Get global context */
trace_ctx_t* trace_get_global(void) {
    return g_trace_ctx;
}

/* Get current trace level */
trace_level_t trace_get_level(void) {
    if (!g_trace_ctx) {
        return TRACE_LEVEL_OFF;
    }
    return g_trace_ctx->config.level;
}

/* Set trace level */
void trace_config_set_level(trace_level_t level) {
    if (!g_trace_ctx) {
        return;
    }
    g_trace_ctx->config.level = level;
    g_trace_ctx->enabled = (level > TRACE_LEVEL_OFF);
}

/* Set PC range filter */
void trace_config_set_pc_range(uint64_t start, uint64_t end) {
    if (!g_trace_ctx) {
        return;
    }
    g_trace_ctx->config.pc_start = start;
    g_trace_ctx->config.pc_end = end;
}

/* Enable event by ID */
void trace_config_enable_event(trace_event_id_t event) {
    if (!g_trace_ctx || event >= TRACE_EVENT_MAX) {
        return;
    }
    g_trace_ctx->config.event_mask |= (1ULL << event);
}

/* Disable event by ID */
void trace_config_disable_event(trace_event_id_t event) {
    if (!g_trace_ctx || event >= TRACE_EVENT_MAX) {
        return;
    }
    g_trace_ctx->config.event_mask &= ~(1ULL << event);
}

/* Enable category */
void trace_config_enable_category(trace_category_t category) {
    if (!g_trace_ctx) {
        return;
    }
    g_trace_ctx->config.category_mask |= category;
}

/* Emit simple event */
void trace_emit(trace_event_id_t event, uint64_t pc) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check if event should be emitted (level, PC filter, etc) */
    if (!trace_event_enabled(event, pc)) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 && 
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event;
    record.header.level = event_descriptors[event].level;
    record.header.cpu_id = 0;  /* TODO: thread-local CPU ID */
    record.header.pc = pc;
    record.header.payload_size = 0;
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit with 8-byte payload */
void trace_emit_u64(trace_event_id_t event, uint64_t pc, uint64_t val) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check if event should be emitted (level, PC filter, etc) */
    if (!trace_event_enabled(event, pc)) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 && 
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event;
    record.header.level = event_descriptors[event].level;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 8;
    
    memcpy(record.payload, &val, 8);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit with 4-byte payload */
void trace_emit_u32(trace_event_id_t event, uint64_t pc, uint32_t val) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check if event should be emitted (level, PC filter, etc) */
    if (!trace_event_enabled(event, pc)) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 && 
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event;
    record.header.level = event_descriptors[event].level;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 4;
    
    memcpy(record.payload, &val, 4);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit fault event */
void trace_emit_fault(uint64_t pc, uint64_t fault_addr, int is_write, int reason) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check if event should be emitted (level, PC filter, etc) */
    if (!trace_event_enabled(TRACE_EVENT_FAULT, pc)) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 && 
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_FAULT;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 24;
    
    memcpy(record.payload + 0, &fault_addr, 8);
    memcpy(record.payload + 8, &is_write, 4);
    memcpy(record.payload + 12, &reason, 4);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit syscall enter */
void trace_emit_syscall_enter(uint64_t pc, uint64_t num, uint64_t x0, uint64_t x1, uint64_t x2) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check if event should be emitted (level, PC filter, etc) */
    if (!trace_event_enabled(TRACE_EVENT_SYSCALL_ENTER, pc)) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 && 
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_SYSCALL_ENTER;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 32;
    
    memcpy(record.payload + 0, &num, 8);
    memcpy(record.payload + 8, &x0, 8);
    memcpy(record.payload + 16, &x1, 8);
    memcpy(record.payload + 24, &x2, 8);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit syscall return */
void trace_emit_syscall_return(uint64_t pc, uint64_t retval) {
    trace_emit_u64(TRACE_EVENT_SYSCALL_RETURN, pc, retval);
}

/* Emit process entry */
void trace_emit_process_entry(uint64_t entry_pc, uint64_t sp, uint64_t at_entry, uint64_t at_base) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check if event should be emitted (level, PC filter, etc) */
    if (!trace_event_enabled(TRACE_EVENT_PROCESS_ENTRY, entry_pc)) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 &&
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_PROCESS_ENTRY;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = entry_pc;
    record.header.payload_size = 32;
    
    memcpy(record.payload + 0, &sp, 8);
    memcpy(record.payload + 8, &at_entry, 8);
    memcpy(record.payload + 16, &at_base, 8);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit block compile start */
void trace_emit_block_compile_start(uint64_t pc) {
    trace_emit_u64(TRACE_EVENT_BLOCK_COMPILE_START, pc, pc);
}

/* Emit block compile end */
void trace_emit_block_compile_end(uint64_t pc, uint64_t end_pc, uint32_t insn_count) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 &&
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_BLOCK_COMPILE_END;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 16;
    
    memcpy(record.payload + 0, &end_pc, 8);
    memcpy(record.payload + 8, &insn_count, 4);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit block entry */
void trace_emit_block_entry(uint64_t pc, uint32_t gadget_count) {
    trace_emit_u32(TRACE_EVENT_BLOCK_ENTRY, pc, gadget_count);
}

/* Emit block exit */
void trace_emit_block_exit(uint64_t pc, int exit_reason, uint64_t next_pc) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check if event should be emitted (level, PC filter, etc) */
    if (!trace_event_enabled(TRACE_EVENT_BLOCK_EXIT, pc)) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 &&
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_BLOCK_EXIT;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 16;
    
    memcpy(record.payload + 0, &exit_reason, 4);
    memcpy(record.payload + 8, &next_pc, 8);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit block cache hit */
void trace_emit_block_cache_hit(uint64_t pc, int cache_level) {
    trace_emit_u32(TRACE_EVENT_BLOCK_CACHE_HIT, pc, (uint32_t)cache_level);
}

/* Emit block cache miss */
void trace_emit_block_cache_miss(uint64_t pc) {
    trace_emit(TRACE_EVENT_BLOCK_CACHE_MISS, pc);
}

/* Emit register snapshot */
void trace_emit_register_snapshot(uint64_t pc, const uint64_t *regs, uint32_t reg_mask) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 &&
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_REGISTER_SNAPSHOT;
    record.header.level = TRACE_LEVEL_BLOCK;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 48;
    
    /* Copy up to 6 registers */
    for (int i = 0; i < 6 && i < 32; i++) {
        if (reg_mask & (1U << i)) {
            memcpy(record.payload + i * 8, &regs[i], 8);
        }
    }
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit PSTATE snapshot */
void trace_emit_pstate_snapshot(uint64_t pc, uint64_t pstate, uint64_t nzcv) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 &&
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_PSTATE_SNAPSHOT;
    record.header.level = TRACE_LEVEL_BLOCK;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 16;
    
    memcpy(record.payload + 0, &pstate, 8);
    memcpy(record.payload + 8, &nzcv, 8);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Get event name */
const char* trace_event_name(trace_event_id_t event) {
    if (event >= TRACE_EVENT_MAX) {
        return "UNKNOWN";
    }
    return event_descriptors[event].name;
}

/* Get event description */
const trace_event_desc_t* trace_event_desc(trace_event_id_t event) {
    if (event >= TRACE_EVENT_MAX) {
        return NULL;
    }
    return &event_descriptors[event];
}

/* Flush output */
void trace_flush(void) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->flush) {
        return;
    }
    g_trace_ctx->backend_ops->flush(g_trace_ctx->backend_ctx);
}

/* Check if sidecar should be created */
bool trace_sidecar_enabled(void) {
    if (!g_trace_ctx || !g_trace_ctx->enabled) {
        return false;
    }
    return g_trace_ctx->config.level >= TRACE_LEVEL_BLOCK || 
           g_trace_ctx->config.dump_on_fault;
}

/* Dump on fault */
void trace_dump_on_fault(uint64_t fault_pc, uint64_t fault_addr, int is_write) {
    if (!g_trace_ctx) {
        return;
    }
    
    /* Emit the fault event */
    trace_emit_fault(fault_pc, fault_addr, is_write, 0);
    
    /* Dump ring if configured */
    if (g_trace_ctx->config.dump_on_fault && g_trace_ctx->ring) {
        const char *path = g_trace_ctx->config.output_path[0] ? 
                          g_trace_ctx->config.output_path : "/tmp/ish.trace.ring";
        trace_dump_ring(path);
    }
    
    /* Also dump to stderr for immediate visibility */
    trace_dump_ring_stderr();
}

/* Dump ring to file */
int trace_dump_ring(const char *path) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->dump) {
        return -1;
    }
    return g_trace_ctx->backend_ops->dump(g_trace_ctx->backend_ctx, path);
}

/* Emit unhandled MRS system register read */
void trace_emit_unhandled_mrs(uint64_t pc, uint32_t sysreg) {
    trace_emit_u32(TRACE_EVENT_UNHANDLED_MRS, pc, sysreg);
}

/* Emit unhandled MSR system register write */
void trace_emit_unhandled_msr(uint64_t pc, uint32_t sysreg) {
    trace_emit_u32(TRACE_EVENT_UNHANDLED_MSR, pc, sysreg);
}

/* Emit unknown COMPLEX exit */
void trace_emit_complex_unknown(uint64_t pc, int cat, int subtype) {
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }
    
    /* Check max events limit BEFORE incrementing */
    if (g_trace_ctx->config.max_events > 0 &&
        g_trace_ctx->emitted_count >= g_trace_ctx->config.max_events) {
        g_trace_ctx->enabled = false;
        return;
    }
    
    trace_record_t record;
    memset(&record, 0, sizeof(record));
    
    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_COMPLEX_UNKNOWN;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 4;
    
    uint16_t payload[2] = { (uint16_t)cat, (uint16_t)subtype };
    memcpy(record.payload, payload, 4);
    
    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit COMPLEX decode failure */
void trace_emit_complex_decode_fail(uint64_t pc, uint32_t insn) {
    trace_emit_u32(TRACE_EVENT_COMPLEX_DECODE_FAIL, pc, insn);
}

/* Emit COMPLEX fetch failure */
void trace_emit_complex_fetch_fail(uint64_t pc, int reason) {
    trace_emit_u32(TRACE_EVENT_COMPLEX_FETCH_FAIL, pc, (uint32_t)reason);
}
