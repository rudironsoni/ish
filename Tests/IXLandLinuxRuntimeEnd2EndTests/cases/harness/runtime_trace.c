/*
 * Runtime Trace Harness
 * Emits semantic trace events only.
 */

#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandInstrumentationTracing/trace_types.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void extract_case_id(const char *yaml_path, char *case_id, size_t case_id_size)
{
    const char *last_slash = strrchr(yaml_path, '/');
    if (!last_slash) {
        strncpy(case_id, "UNKNOWN", case_id_size);
        case_id[case_id_size - 1] = '\0';
        return;
    }

    const char *case_dir = last_slash;
    while (case_dir > yaml_path && *(case_dir - 1) != '/')
        case_dir--;

    size_t len = (size_t)(last_slash - case_dir);
    if (len >= case_id_size)
        len = case_id_size - 1;

    memcpy(case_id, case_dir, len);
    case_id[len] = '\0';
}

int run_runtime_trace(const char *case_yaml, const char *artifact_dir)
{
    (void)artifact_dir;

    trace_config_t config = { 0 };
    config.backend = TRACE_BACKEND_RING;
    config.level = TRACE_LEVEL_BOUNDARY;
    config.category_mask = TRACE_CAT_BLOCK;

    if (trace_init(&config) != 0)
        return 1;

    char case_id[64] = "UNKNOWN";
    extract_case_id(case_yaml, case_id, sizeof(case_id));

    if (strcmp(case_id, "TRACE-002") == 0) {
        trace_config_set_pc_range(0x1000, 0x1fff);
        trace_emit_block_entry(0x1000, 4);
        trace_emit_block_exit(0x1004, 0, 0);
        trace_emit_block_entry(0x1800, 2);
        trace_emit_block_entry(0x3000, 4);
    } else if (strcmp(case_id, "TRACE-003") == 0 || strcmp(case_id, "TRACE-003-reg-filter") == 0) {
        trace_config_enable_category(TRACE_CAT_REGISTER);
        trace_config_set_level(TRACE_LEVEL_BLOCK);
        trace_ctx_t *ctx = trace_get_global();
        if (ctx)
            ctx->config.regs_mask = (1U << 0) | (1U << 1) | (1U << 2);

        uint64_t regs[32];
        for (int i = 0; i < 32; i++)
            regs[i] = 0x100000000ULL + (uint64_t)i;

        trace_emit_block_entry(0x1000, 4);
        trace_emit_register_snapshot(0x1000, regs, (1U << 0) | (1U << 1) | (1U << 2));
        trace_emit_block_exit(0x1004, 0, 0);
    } else if (strcmp(case_id, "TRACE-004") == 0) {
        trace_config_enable_category(TRACE_CAT_FAULT);
        trace_ctx_t *ctx = trace_get_global();
        if (ctx)
            ctx->config.dump_on_fault = true;

        trace_emit_block_entry(0x1000, 4);
        trace_emit_block_exit(0x1004, 0, 0x1008);
        trace_emit_fault(0x1010, 0x2000, 1, 2);
    } else if (strcmp(case_id, "TRACE-005") == 0) {
        trace_config_set_level(TRACE_LEVEL_BLOCK);
        trace_block_sidecar_t *sidecar = trace_sidecar_create(0x1000, 0x1020);
        if (sidecar) {
            trace_sidecar_add_insn(sidecar, 0x1000, 0xD2800000, "mov x0, #0");
            trace_sidecar_add_insn(sidecar, 0x1004, 0xD2800021, "mov x1, #1");
            trace_sidecar_add_insn(sidecar, 0x1008, 0x8B010000, "add x0, x0, x1");
            trace_sidecar_set_gadget_count(sidecar, 3);
        }
        trace_emit_block_entry(0x1000, 3);
        trace_emit_block_exit(0x1008, 0, 0);
    } else if (strncmp(case_id, "TRACE-006", 9) == 0) {
        trace_emit_block_entry(0x1000, 4);
        trace_emit_block_exit(0x1004, 0, 0x1008);
        trace_emit_block_entry(0x1008, 3);
        trace_emit_block_exit(0x100c, 1, 0x2000);
        trace_emit_syscall_enter(0x2000, 64, 1, 2, 3);
        trace_emit_syscall_return(0x2004, 4);
    } else {
        trace_emit_block_entry(0x1000, 4);
        trace_emit_block_exit(0x1004, 0, 0x1008);
    }

    trace_shutdown();
    return 0;
}

static int main(int argc, char *argv[])
{
    const char *case_yaml = NULL;
    const char *artifact_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--case-yaml") == 0 && i + 1 < argc)
            case_yaml = argv[++i];
        else if (strcmp(argv[i], "--artifact-dir") == 0 && i + 1 < argc)
            artifact_dir = argv[++i];
    }

    if (!case_yaml || !artifact_dir)
        return 1;

    return run_runtime_trace(case_yaml, artifact_dir);
}
