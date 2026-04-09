/*
 * trace_dump.c
 * Minimal stubs for legacy dump API.
 */

#include "trace.h"
#include "trace_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

trace_block_sidecar_t *trace_sidecar_create(uint64_t start_pc, uint64_t end_pc)
{
    (void)start_pc;
    (void)end_pc;
    return NULL;
}

void trace_sidecar_add_insn(trace_block_sidecar_t *sidecar, uint64_t pc, uint32_t raw_insn,
                            const char *mnemonic)
{
    (void)sidecar;
    (void)pc;
    (void)raw_insn;
    (void)mnemonic;
}

void trace_sidecar_set_regs(trace_block_sidecar_t *sidecar, int insn_idx, const uint8_t *dst_regs,
                            int num_dsts, const uint8_t *src_regs, int num_srcs)
{
    (void)sidecar;
    (void)insn_idx;
    (void)dst_regs;
    (void)num_dsts;
    (void)src_regs;
    (void)num_srcs;
}

void trace_sidecar_set_gadget_count(trace_block_sidecar_t *sidecar, uint32_t count)
{
    (void)sidecar;
    (void)count;
}

void trace_sidecar_dump(trace_block_sidecar_t *sidecar, FILE *fp)
{
    (void)sidecar;
    (void)fp;
}

bool trace_sidecar_enabled(void)
{
    return false;
}

void trace_dump_on_fault(uint64_t fault_pc, uint64_t fault_addr, int is_write)
{
    (void)fault_pc;
    (void)fault_addr;
    (void)is_write;
}

int trace_dump_ring(const char *path)
{
    (void)path;
    return 0;
}
