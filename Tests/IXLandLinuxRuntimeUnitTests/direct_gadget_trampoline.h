#ifndef DIRECT_GADGET_TRAMPOLINE_H
#define DIRECT_GADGET_TRAMPOLINE_H

#include <stdint.h>

typedef void (*tcti_gadget_t)(void);

void test_tcti_single_gadget_snapshot(tcti_gadget_t gadget, const uint64_t *in_regs,
                                      uint64_t *out_regs);

#endif
