/*
 * TCTI Boundary Probe - Assembly gadgets for four-boundary capture
 * 
 * This is test-only code, NOT product code.
 * Naked assembly probes that capture host register state and chain.
 */

.text
.align 4

/* External symbol from tcti_boundary_probe.c - reference only */
.extern _active_probe_buffer

/*
 * tcti_probe_capture_b2_x3_x8
 * 
 * B2 probe: captures host x3 and x8 after entry
 * These are the live carriers for guest x2 and x7
 * 
 * Register contract:
 *   x28 = bytecode pointer (preserved)
 *   x29 = cpu_state pointer (preserved)
 *   x3, x8 = values to capture (preserved)
 * 
 * Epilogue: ldr x27, [x28], #8; br x27
 */
.global _tcti_probe_capture_b2_x3_x8
.global tcti_probe_capture_b2_x3_x8
_tcti_probe_capture_b2_x3_x8:
tcti_probe_capture_b2_x3_x8:
    /* Load active probe buffer pointer from C global */
    adrp    x17, _active_probe_buffer@PAGE
    add     x17, x17, _active_probe_buffer@PAGEOFF
    ldr     x17, [x17]              /* x17 = active_probe_buffer */
    cbz     x17, 1f                 /* If NULL, skip capture and just chain */
    
    /* Store host x3 (guest x2 carrier) to b2_post_entry_host_x3 */
    str     x3, [x17, #0]
    
    /* Store host x8 (guest x7 carrier) to b2_post_entry_host_x8 */
    str     x8, [x17, #8]
    
1:
    /* Standard TCTI epilogue - chain to next gadget */
    ldr     x27, [x28], #8
    br      x27

/*
 * tcti_probe_capture_b3_x3_x8
 * 
 * B3 probe: captures host x3 and x8 after gadget execution
 * These are the live carriers for guest x2 and x7
 * 
 * Register contract:
 *   x28 = bytecode pointer (preserved)
 *   x29 = cpu_state pointer (preserved)
 *   x3, x8 = values to capture (preserved)
 * 
 * Epilogue: ldr x27, [x28], #8; br x27
 */
.global _tcti_probe_capture_b3_x3_x8
.global tcti_probe_capture_b3_x3_x8
_tcti_probe_capture_b3_x3_x8:
tcti_probe_capture_b3_x3_x8:
    /* Load active probe buffer pointer from C global */
    adrp    x17, _active_probe_buffer@PAGE
    add     x17, x17, _active_probe_buffer@PAGEOFF
    ldr     x17, [x17]              /* x17 = active_probe_buffer */
    cbz     x17, 1f                 /* If NULL, skip capture and just chain */
    
    /* Store host x3 (guest x2 carrier) to b3_post_gadget_host_x3 */
    str     x3, [x17, #16]
    
    /* Store host x8 (guest x7 carrier) to b3_post_gadget_host_x8 */
    str     x8, [x17, #24]
    
1:
    /* Standard TCTI epilogue - chain to next gadget */
    ldr     x27, [x28], #8
    br      x27

/*
 * tcti_probe_capture_b35_x3_x8_then_exit
 * 
 * B3.5 probe: captures host x3 and x8 immediately before exit flush
 * Then jumps into the same exit path as gadget_exit
 * 
 * Register contract:
 *   x28 = bytecode pointer (not needed after this)
 *   x29 = cpu_state pointer (preserved for exit block)
 *   x3, x8 = values to capture (preserved for exit block)
 * 
 * After capture: sets x0 = 0 (normal exit), branches to _tcti_exit_block
 */
.global _tcti_probe_capture_b35_x3_x8_then_exit
.global tcti_probe_capture_b35_x3_x8_then_exit
_tcti_probe_capture_b35_x3_x8_then_exit:
tcti_probe_capture_b35_x3_x8_then_exit:
    /* Load active probe buffer pointer from C global */
    adrp    x17, _active_probe_buffer@PAGE
    add     x17, x17, _active_probe_buffer@PAGEOFF
    ldr     x17, [x17]              /* x17 = active_probe_buffer */
    cbz     x17, 1f                 /* If NULL, skip capture and just exit */
    
    /* Store host x3 (guest x2 carrier) to b35_pre_flush_host_x3 */
    str     x3, [x17, #32]
    
    /* Store host x8 (guest x7 carrier) to b35_pre_flush_host_x8 */
    str     x8, [x17, #40]
    
1:
    /* Same exit path as gadget_exit: set x0 = 0, branch to exit block */
    mov     x0, #0
    b       _tcti_exit_block

/*
 * TEST-ONLY CONTROL GADGET
 * tcti_testonly_control_mov_x3_x8
 * 
 * This is a test-only control gadget that does exactly what gadget_mov_reg_2_7
 * should do: mov x3, x8 (move host x8 value to host x3)
 * 
 * This allows us to isolate whether HC005 failures are due to:
 * - the product gadget implementation (gadget_mov_reg_2_7 body)
 * - the gadget dispatch/chain path
 * - probe timing
 * 
 * Contract: mov x3, x8, then chain to next gadget
 */
.global _tcti_testonly_control_mov_x3_x8
.global tcti_testonly_control_mov_x3_x8
_tcti_testonly_control_mov_x3_x8:
tcti_testonly_control_mov_x3_x8:
    /* Execute MOV x3, x8 (same as product gadget_mov_reg_2_7) */
    mov     x3, x8
    /* Standard TCTI epilogue - chain to next gadget */
    ldr     x27, [x28], #8
    br      x27
