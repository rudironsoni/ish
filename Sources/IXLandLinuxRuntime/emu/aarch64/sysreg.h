#ifndef EMU_AARCH64_SYSREG_H
#define EMU_AARCH64_SYSREG_H

#import "decode.h"

#include <stdint.h>

struct cpu_state;

#define TCTI_EXIT_UNSUPPORTED_SYSREG 5

enum a64_sysreg_tier {
    A64_SYSREG_TIER0 = 0,
    A64_SYSREG_TIER1 = 1,
    A64_SYSREG_TIER2 = 2,
};

enum a64_sysreg_route {
    A64_SYSREG_ROUTE_UNSUPPORTED = 0,
    A64_SYSREG_ROUTE_TCTI_FASTPATH = 1,
    A64_SYSREG_ROUTE_COMPLEX_FALLBACK = 2,
};

typedef struct a64_sysreg_spec {
    uint16_t encoding;
    const char *name;
    enum a64_sysreg_tier tier;
    enum a64_sysreg_route read_route;
    enum a64_sysreg_route write_route;
    const char *read_test;
    const char *write_test;
} a64_sysreg_spec_t;

enum {
    A64_SYSREG_NZCV = 0x5A10,
    A64_SYSREG_DAIF = 0x1008,
    A64_SYSREG_CTR_EL0 = 0x5801,
    A64_SYSREG_DCZID_EL0 = 0x5807,
    A64_SYSREG_FPCR = 0x5A20,
    A64_SYSREG_FPSR = 0x5A21,
    A64_SYSREG_CNTFRQ_EL0 = 0x5F00,
    A64_SYSREG_CNTVCT_EL0 = 0x5F02,
    A64_SYSREG_TPIDR_EL0 = 0x5E82,
    A64_SYSREG_TPIDRRO_EL0 = 0x5E83,
};

enum {
    // 64-byte I-cache and D-cache minimum line sizes (16 words each).
    A64_SYSREG_CTR_EL0_VALUE = 0x00040004,
    // DZP=1: DC ZVA is prohibited at EL0 in this runtime.
    A64_SYSREG_DCZID_EL0_VALUE = 0x00000010,
};

const a64_sysreg_spec_t *a64_sysreg_lookup(uint16_t sysreg);
const char *a64_sysreg_name(uint16_t sysreg);
enum a64_sysreg_route a64_sysreg_route_for_access(uint16_t sysreg, int is_write);
int a64_sysreg_read(struct cpu_state *cpu, uint16_t sysreg, uint64_t rd);
int a64_sysreg_write(struct cpu_state *cpu, uint16_t sysreg, uint64_t rt);
int a64_sysreg_handle_complex(struct cpu_state *cpu, const a64_instr_t *instr);

#endif
