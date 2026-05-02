#ifndef TCTI_HARNESS_TRUTH_H
#define TCTI_HARNESS_TRUTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TCTI_HARNESS_INPUT_REGS = 16,
    TCTI_HARNESS_SNAPSHOT_REGS = 20,
};

typedef struct tcti_harness_snapshot {
    uint64_t in_regs[TCTI_HARNESS_INPUT_REGS];
    uint64_t out_regs[TCTI_HARNESS_SNAPSHOT_REGS];
} tcti_harness_snapshot_t;

void tcti_harness_seed_inputs(uint64_t regs[TCTI_HARNESS_INPUT_REGS]);
void tcti_harness_run_snapshot(void (*gadget)(void),
                               const uint64_t in_regs[TCTI_HARNESS_INPUT_REGS],
                               uint64_t out_regs[TCTI_HARNESS_SNAPSHOT_REGS]);

int tcti_harness_case_add_7_13_14(tcti_harness_snapshot_t *snapshot);
int tcti_harness_case_add_0_1_2(tcti_harness_snapshot_t *snapshot);
int tcti_harness_case_mov_2_7(tcti_harness_snapshot_t *snapshot);
uint64_t tcti_harness_case_entry_restores_pstate_for_bcond_ne(void);
uint64_t tcti_harness_case_cmp_w20_ccmp_gt_bls_uses_32bit_flags(void);
uint64_t tcti_harness_case_cmp_w2_w1_uxtb_csel_uses_w_width(void);
uint64_t tcti_harness_case_csel_eq_selects_true_operand(void);
uint64_t tcti_harness_case_csel_preserves_flags_for_bcond(void);
uint64_t tcti_harness_case_strchrnul_vector_mask_finds_dot(void);
uint64_t tcti_harness_case_logical_mov_roundtrips_memory_backed_x19(void);
uint64_t tcti_harness_case_cset_eq_then_add_to_x3(void);
uint64_t tcti_harness_case_ldr_x5_from_memory_backed_x27(void);
uint64_t tcti_harness_case_str_x0_to_memory_backed_x22_scaled_x1(void);
uint64_t tcti_harness_case_add_hot_pair_to_memory_backed_x23(void);
uint64_t tcti_harness_case_cmp_memory_backed_x27_x23_branches_eq(void);
uint64_t tcti_harness_case_stack_pair_roundtrips_hot_x5_x4(void);
uint64_t tcti_harness_case_dynamic_tag_scaled_store_uses_full_index(void);
uint64_t tcti_harness_case_pltrel_rela_stride_selector(void);
uint64_t tcti_harness_case_relocation_loop_preserves_loaded_x5(void);
uint64_t tcti_harness_case_relocation_fault_path_uses_loaded_x5(void);

#ifdef __cplusplus
}
#endif

#endif
