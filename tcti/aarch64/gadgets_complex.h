/*
 * Complex TCTI Gadgets Header
 * 
 * Function declarations for assembly gadgets that handle:
 * - Load/Store with TLB translation
 * - Bitfield operations (SBFM, UBFM)
 * - CBZ/CBNZ branches
 * - System register access
 */

#ifndef AARCH64_TCTI_GADGETS_COMPLEX_H
#define AARCH64_TCTI_GADGETS_COMPLEX_H

#include "misc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Load/Store Gadgets
 * ============================================================================ */

// Load with immediate offset
extern void gadget_ldr_imm(void);

// Store with immediate offset  
extern void gadget_str_imm(void);

/* ============================================================================
 * System Gadgets
 * ============================================================================ */

// Note: gadget_mrs and gadget_msr are defined in gadgets_tcti.h
// They use tcti_gadget_t type (function pointer) not void

#ifdef __cplusplus
}
#endif

#endif /* AARCH64_TCTI_GADGETS_COMPLEX_H */
