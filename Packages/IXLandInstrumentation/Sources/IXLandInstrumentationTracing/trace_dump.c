/*
 * trace_dump.c
 * Block sidecar implementation and dump utilities.
 */

#include "trace.h"
#include "trace_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration for block structure - used by trace_sidecar_dump_block */
struct a64_block {
    uint64_t start_pc;
    void *trace_sidecar;
    /* Other fields omitted - only what we need for trace_sidecar_dump_block */
};

/* Create sidecar for a block */
trace_block_sidecar_t *trace_sidecar_create(uint64_t start_pc, uint64_t end_pc)
{
    if (!trace_sidecar_enabled()) {
        return NULL;
    }

    trace_block_sidecar_t *sidecar = calloc(1, sizeof(trace_block_sidecar_t));
    if (!sidecar) {
        return NULL;
    }

    sidecar->start_pc = start_pc;
    sidecar->end_pc = end_pc;
    sidecar->explicit_pc_on_exit = false;
    sidecar->insn_count = 0;
    sidecar->gadget_count = 0;

    return sidecar;
}

/* Add instruction to sidecar */
void trace_sidecar_add_insn(trace_block_sidecar_t *sidecar, uint64_t pc, uint32_t raw_insn,
                            const char *mnemonic)
{
    if (!sidecar || sidecar->insn_count >= TRACE_MAX_SIDECAR_INSNS) {
        return;
    }

    trace_sidecar_insn_t *insn = &sidecar->insns[sidecar->insn_count];
    insn->pc = pc;
    insn->raw_insn = raw_insn;

    if (mnemonic) {
        strncpy(insn->mnemonic, mnemonic, TRACE_MAX_MNEMONIC_LEN - 1);
        insn->mnemonic[TRACE_MAX_MNEMONIC_LEN - 1] = '\0';
    } else {
        insn->mnemonic[0] = '\0';
    }

    insn->num_dsts = 0;
    insn->num_srcs = 0;

    sidecar->insn_count++;
}

/* Set instruction register info */
void trace_sidecar_set_regs(trace_block_sidecar_t *sidecar, int insn_idx, const uint8_t *dst_regs,
                            int num_dsts, const uint8_t *src_regs, int num_srcs)
{
    if (!sidecar || insn_idx < 0 || insn_idx >= (int)sidecar->insn_count) {
        return;
    }

    trace_sidecar_insn_t *insn = &sidecar->insns[insn_idx];

    insn->num_dsts = num_dsts < 4 ? num_dsts : 4;
    for (int i = 0; i < insn->num_dsts; i++) {
        insn->dst_regs[i] = dst_regs[i];
    }

    insn->num_srcs = num_srcs < 4 ? num_srcs : 4;
    for (int i = 0; i < insn->num_srcs; i++) {
        insn->src_regs[i] = src_regs[i];
    }
}

/* Set gadget count for sidecar */
void trace_sidecar_set_gadget_count(trace_block_sidecar_t *sidecar, uint32_t count)
{
    if (!sidecar)
        return;
    sidecar->gadget_count = count;
}

/* Dump sidecar to file */
void trace_sidecar_dump(trace_block_sidecar_t *sidecar, FILE *fp)
{
    if (!sidecar || !fp)
        return;

    fprintf(fp, "=== Block Sidecar ===\n");
    fprintf(fp, "Start PC: 0x%016llx\n", (unsigned long long)sidecar->start_pc);
    fprintf(fp, "End PC:   0x%016llx\n", (unsigned long long)sidecar->end_pc);
    fprintf(fp, "Explicit PC on exit: %s\n", sidecar->explicit_pc_on_exit ? "yes" : "no");
    fprintf(fp, "Instruction count: %u\n", sidecar->insn_count);
    fprintf(fp, "Gadget count: %u\n", sidecar->gadget_count);
    fprintf(fp, "\n");

    if (sidecar->insn_count > 0) {
        fprintf(fp, "Instructions:\n");
        fprintf(fp, "%-4s %-18s %-12s %-32s %-20s\n", "Idx", "PC", "Raw", "Mnemonic", "Regs");
        fprintf(
            fp,
            "--------------------------------------------------------------------------------\n");

        for (uint32_t i = 0; i < sidecar->insn_count; i++) {
            trace_sidecar_insn_t *insn = &sidecar->insns[i];

            /* Build register string */
            char reg_str[64] = { 0 };
            int pos = 0;

            if (insn->num_dsts > 0) {
                pos += snprintf(reg_str + pos, sizeof(reg_str) - pos, "D:");
                for (int j = 0; j < insn->num_dsts && pos < (int)(sizeof(reg_str) - 4); j++) {
                    pos += snprintf(reg_str + pos, sizeof(reg_str) - pos, "x%d", insn->dst_regs[j]);
                    if (j < insn->num_dsts - 1) {
                        pos += snprintf(reg_str + pos, sizeof(reg_str) - pos, ",");
                    }
                }
            }

            if (insn->num_srcs > 0) {
                if (insn->num_dsts > 0)
                    pos += snprintf(reg_str + pos, sizeof(reg_str) - pos, " ");
                pos += snprintf(reg_str + pos, sizeof(reg_str) - pos, "S:");
                for (int j = 0; j < insn->num_srcs && pos < (int)(sizeof(reg_str) - 4); j++) {
                    pos += snprintf(reg_str + pos, sizeof(reg_str) - pos, "x%d", insn->src_regs[j]);
                    if (j < insn->num_srcs - 1) {
                        pos += snprintf(reg_str + pos, sizeof(reg_str) - pos, ",");
                    }
                }
            }

            fprintf(fp, "%-4u 0x%016llx 0x%08x   %-32s %-20s\n", i, (unsigned long long)insn->pc,
                    insn->raw_insn, insn->mnemonic[0] ? insn->mnemonic : "(unknown)", reg_str);
        }
    }

    fprintf(fp, "=== End Block Sidecar ===\n");
}

/* Dump sidecar for a block to stderr */
void trace_sidecar_dump_block(struct a64_block *block)
{
    if (!block || !block->trace_sidecar) {
        fprintf(stderr, "[TRACE] No sidecar available for block at 0x%llx\n",
                (unsigned long long)(block ? block->start_pc : 0));
        return;
    }

    fprintf(stderr, "\n");
    trace_sidecar_dump(block->trace_sidecar, stderr);
    fprintf(stderr, "\n");
}

/* Free sidecar memory */
void trace_sidecar_free(trace_block_sidecar_t *sidecar)
{
    if (sidecar) {
        free(sidecar);
    }
}
