/*
 * TCTI Boundary Probe - Test-only instrumentation for four-boundary capture
 *
 * This is test-only code, NOT product code.
 * Used by HostCarrierContractTests to capture exact boundaries.
 */

#ifndef TCTI_BOUNDARY_PROBE_H
#define TCTI_BOUNDARY_PROBE_H

#include <stdint.h>

/*
 * Boundary capture buffer
 * Captures host register values at exact probe points
 */
struct tcti_boundary_probe_buffer {
    uint64_t b2_post_entry_host_x3;  /* B2: host x3 after entry (guest x2 carrier) */
    uint64_t b2_post_entry_host_x8;  /* B2: host x8 after entry (guest x7 carrier) */
    uint64_t b3_post_gadget_host_x3; /* B3: host x3 after gadget (guest x2 carrier) */
    uint64_t b3_post_gadget_host_x8; /* B3: host x8 after gadget (guest x7 carrier) */
    uint64_t b35_pre_flush_host_x3;  /* B3.5: host x3 before exit flush */
    uint64_t b35_pre_flush_host_x8;  /* B3.5: host x8 before exit flush */
};

/* Global probe buffer pointer - accessed by assembly probes */
extern struct tcti_boundary_probe_buffer *active_probe_buffer;

/* Set the active probe buffer - called by test setup */
void tcti_boundary_probe_set_buffer(struct tcti_boundary_probe_buffer *buf);

/* Clear the active probe buffer - called by test teardown */
void tcti_boundary_probe_clear_buffer(void);

/*
 * Probe gadgets - use as regular gadgets in the chain
 * These are naked assembly functions that capture and chain
 */
void tcti_probe_capture_b2_x3_x8(void);
void tcti_probe_capture_b3_x3_x8(void);

/*
 * B3.5 probe - captures immediately before exit, then enters exit block
 * Does NOT chain - it exits directly to tcti_exit_block
 */
void tcti_probe_capture_b35_x3_x8_then_exit(void);

/*
 * TEST-ONLY CONTROL GADGET
 * tcti_testonly_control_mov_x3_x8
 *
 * Test-only gadget that does: mov x3, x8; chain
 * Used to isolate HC005 failures from product gadget vs dispatch issues
 */
void tcti_testonly_control_mov_x3_x8(void);

#endif /* TCTI_BOUNDARY_PROBE_H */
