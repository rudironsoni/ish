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

#ifdef __cplusplus
}
#endif

#endif
