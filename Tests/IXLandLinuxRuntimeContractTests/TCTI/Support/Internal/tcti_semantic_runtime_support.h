#ifndef TCTI_SEMANTIC_RUNTIME_SUPPORT_H
#define TCTI_SEMANTIC_RUNTIME_SUPPORT_H

#include <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
#include <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#include <IXLandLinuxRuntime/emu/aarch64/memory.h>
#include <IXLandLinuxRuntime/emu/tlb.h>
#include <IXLandLinuxRuntime/kernel/memory.h>
#include <string.h>

typedef void (*tcti_gadget_t)(void);

extern const tcti_gadget_t gadget_add_reg[16][16][16];
extern const tcti_gadget_t gadget_mov_reg[16][16];
extern const tcti_gadget_t gadget_bcond[16];
extern tcti_gadget_t gadget_addsub_imm_fallback;
extern tcti_gadget_t gadget_addsub_ext_fallback;
extern tcti_gadget_t gadget_addsub_reg_fallback;
extern tcti_gadget_t gadget_ccmp_fallback;
extern tcti_gadget_t gadget_csel_fallback;
extern tcti_gadget_t gadget_extend_x14;
extern tcti_gadget_t gadget_exit;
extern tcti_gadget_t gadget_ldr_x;
extern tcti_gadget_t gadget_logical_reg_fallback;
extern tcti_gadget_t gadget_pc_advance;
extern tcti_gadget_t gadget_str_x;
extern int _a64_tcti_ldr_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
                                  uint64_t rn, int64_t imm, uint64_t size, uint64_t idx_mode,
                                  uint64_t meta);
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);
extern void tcti_exit_block(int reason);

#endif
