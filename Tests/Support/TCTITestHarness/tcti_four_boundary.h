#ifndef TCTI_FOUR_BOUNDARY_H
#define TCTI_FOUR_BOUNDARY_H

#include <stdint.h>

/* Forward declarations to avoid include hell */
struct cpu_state;
typedef void (*tcti_gadget_t)(void);

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Four-boundary snapshot structure for autonomous host-carrier debugging.
 *
 * Contract per docs/tcti/register-map.yaml:
 * - guest x2 -> host x3
 * - guest x7 -> host x8
 */
typedef struct tcti_four_boundary_snapshot {
    /* B1: Pre-entry guest-visible state */
    uint64_t b1_pre_guest_x2;
    uint64_t b1_pre_guest_x7;

    /* B2: Post-entry live hot carriers */
    uint64_t b2_post_entry_host_x3; /* guest x2 in host x3 */
    uint64_t b2_post_entry_host_x8; /* guest x7 in host x8 */

    /* B3: Post-gadget live hot carriers */
    uint64_t b3_post_gadget_host_x8; /* guest x7 after gadget */

    /* B4: Post-exit flushed guest-visible state */
    uint64_t b4_post_exit_guest_x7;
} tcti_four_boundary_snapshot_t;

/*
 * Execute TCTI path with four-boundary capture for MOV x7, x2.
 *
 * Captures:
 * - B1: guest values before entry
 * - B2: host values after entry load (x3, x8)
 * - B3: host values after gadget execution (x8)
 * - B4: guest values after exit flush
 */
void tcti_execute_mov_x7_x2_with_boundaries(struct cpu_state *cpu,
                                            tcti_four_boundary_snapshot_t *snap);

#ifdef __cplusplus
}
#endif

#endif
