#ifndef TCTI_CONTROL_AND_FLAGS_SEMANTIC_SCENARIOS_H
#define TCTI_CONTROL_AND_FLAGS_SEMANTIC_SCENARIOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TCTI_SEMANTIC_INPUT_REGS = 16,
    TCTI_SEMANTIC_SNAPSHOT_REGS = 20,
};

typedef struct tcti_semantic_snapshot {
    uint64_t in_regs[TCTI_SEMANTIC_INPUT_REGS];
    uint64_t out_regs[TCTI_SEMANTIC_SNAPSHOT_REGS];
} tcti_semantic_snapshot_t;

void tcti_semantic_seed_inputs(uint64_t regs[TCTI_SEMANTIC_INPUT_REGS]);
void tcti_semantic_run_snapshot(void (*gadget)(void),
                                const uint64_t in_regs[TCTI_SEMANTIC_INPUT_REGS],
                                uint64_t out_regs[TCTI_SEMANTIC_SNAPSHOT_REGS]);

int tcti_semantic_case_add_7_13_14(tcti_semantic_snapshot_t *snapshot);
uint64_t tcti_semantic_case_busybox_prompt_first_turn_ldurb_csel_hi_block(void);
uint64_t tcti_semantic_case_busybox_prompt_loop_ccmp_bls_exits_taken_path(void);
uint64_t tcti_semantic_case_cmp_add_csel_ne_uses_preserved_zero_flag(void);
uint64_t tcti_semantic_case_cmp_ccmp_false_immediate_clears_zero(void);
uint64_t tcti_semantic_case_cmp_csel_ls_hs_tracks_unsigned_minmax(void);
uint64_t tcti_semantic_case_cmp_csinv_ls_preserves_nonoverflow_size(void);
uint64_t tcti_semantic_case_cmp_csinv_ls_saturates_overflow_size(void);
uint64_t tcti_semantic_case_cmp_w20_ccmp_gt_bls_uses_32bit_flags(void);
uint64_t tcti_semantic_case_cmp_w2_w1_uxtb_csel_uses_w_width(void);
uint64_t tcti_semantic_case_csel_eq_selects_true_operand(void);
uint64_t tcti_semantic_case_csel_preserves_flags_for_bcond(void);
uint64_t tcti_semantic_case_entry_restores_pstate_for_bcond_ne(void);
uint64_t tcti_semantic_case_generated_cinc_ne_increments_only_on_ne(void);
uint64_t tcti_semantic_case_generated_cinv_w_ne_zero_extends(void);
uint64_t tcti_semantic_case_generated_cneg_w_ne_zero_extends(void);
uint64_t tcti_semantic_case_generated_csetm_w_ne_zero_extends(void);
uint64_t tcti_semantic_case_generated_vsnprintf_zero_size_length(void);
int tcti_semantic_case_mov_2_7(tcti_semantic_snapshot_t *snapshot);
uint64_t tcti_semantic_case_musl_memset_dup_replicates_byte_fill(void);
uint64_t tcti_semantic_case_musl_memset_dup_zeroes_vector_store(void);
uint64_t tcti_semantic_case_strchrnul_byte_loop_stops_on_match_or_nul(void);
uint64_t tcti_semantic_case_strchrnul_vector_mask_finds_dot(void);
uint64_t tcti_semantic_case_tpidr_el0_roundtrips_through_full_sysreg_encoding(void);
uint64_t tcti_semantic_case_vsnprintf_zero_size_cset_ne_preserves_zero_flag(void);

#ifdef __cplusplus
}
#endif

#endif
