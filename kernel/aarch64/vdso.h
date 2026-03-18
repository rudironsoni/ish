/*
 * aarch64 VDSO definitions
 */

#ifndef AARCH64_VDSO_H
#define AARCH64_VDSO_H

#include "misc.h"

// VDSO is mapped at a high address in the guest address space
#define A64_VDSO_BASE 0x7fff00000000ULL

// Signal trampoline offset within vdso
#define A64_SIGTRAMP_OFFSET 0

// Initialize the vdso page
int a64_init_vdso(void);

// Get the address of the signal trampoline
uint64_t a64_get_vdso_sigtramp(void);

// Get vdso page for mapping
const void *a64_get_vdso_page(size_t *size);

// Setup vdso in a process
struct mm;
int a64_setup_vdso(struct mm *mm);

#endif
