#include "tcti_four_boundary.h"

#include <string.h>

/* Assembly helpers to read host registers */
static inline uint64_t read_x3(void)
{
    uint64_t val;
    __asm__ volatile("mov %0, x3" : "=r"(val));
    return val;
}

static inline uint64_t read_x8(void)
{
    uint64_t val;
    __asm__ volatile("mov %0, x8" : "=r"(val));
    return val;
}

/* External declarations for TCTI entry and gadgets */
extern void tcti_entry_block(void **gadgets, struct cpu_state *cpu);
extern void gadget_mov_reg_7_2(void);
extern void gadget_exit(void);

/*
 * Probe gadget that captures B2 and B3 live carriers.
 * Called from within TCTI context.
 */
static void probe_capture_b2_b3(tcti_four_boundary_snapshot_t *snap)
{
    /* B2: Post-entry live carriers */
    snap->b2_post_entry_host_x3 = read_x3();
    snap->b2_post_entry_host_x8 = read_x8();

    /* Execute MOV x7, x2 gadget */
    gadget_mov_reg_7_2();

    /* B3: Post-gadget live carriers */
    snap->b3_post_gadget_host_x8 = read_x8();
}

/*
 * Execute MOV x7, x2 with four-boundary capture.
 */
void tcti_execute_mov_x7_x2_with_boundaries(struct cpu_state *cpu,
                                            tcti_four_boundary_snapshot_t *snap)
{
    void *gadgets[2];

    memset(snap, 0, sizeof(*snap));

    /* B1: Pre-entry guest state */
    snap->b1_pre_guest_x2 = cpu->x[2];
    snap->b1_pre_guest_x7 = cpu->x[7];

    /* Build probe gadget stream */
    gadgets[0] = (void *)probe_capture_b2_b3;
    gadgets[1] = (void *)gadget_exit;

    /* Execute through TCTI path */
    tcti_entry_block(gadgets, cpu);

    /* B4: Post-exit guest state */
    snap->b4_post_exit_guest_x7 = cpu->x[7];
}
