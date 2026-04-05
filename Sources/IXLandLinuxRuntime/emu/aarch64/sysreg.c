#import "sysreg.h"

#import "cpu.h"

#include <IXLandLinuxRuntime/tcti/gadgets_tcti.h>

static const a64_sysreg_spec_t a64_sysreg_specs[] = {
    { A64_SYSREG_NZCV, "NZCV", A64_SYSREG_TIER0, A64_SYSREG_ROUTE_TCTI_FASTPATH,
      A64_SYSREG_ROUTE_TCTI_FASTPATH, "EXEC-016", "EXEC-016" },
    { A64_SYSREG_TPIDR_EL0, "TPIDR_EL0", A64_SYSREG_TIER0, A64_SYSREG_ROUTE_TCTI_FASTPATH,
      A64_SYSREG_ROUTE_TCTI_FASTPATH, "sysreg_test:tpidr_el0_roundtrip",
      "sysreg_test:tpidr_el0_roundtrip" },
    { A64_SYSREG_TPIDRRO_EL0, "TPIDRRO_EL0", A64_SYSREG_TIER0, A64_SYSREG_ROUTE_TCTI_FASTPATH,
      A64_SYSREG_ROUTE_UNSUPPORTED, "sysreg_test:tpidrro_el0_read",
      "sysreg_test:unsupported_routes" },
    { A64_SYSREG_FPCR, "FPCR", A64_SYSREG_TIER1, A64_SYSREG_ROUTE_TCTI_FASTPATH,
      A64_SYSREG_ROUTE_TCTI_FASTPATH, "sysreg_test:fpcr_roundtrip", "sysreg_test:fpcr_roundtrip" },
    { A64_SYSREG_FPSR, "FPSR", A64_SYSREG_TIER1, A64_SYSREG_ROUTE_TCTI_FASTPATH,
      A64_SYSREG_ROUTE_TCTI_FASTPATH, "sysreg_test:fpsr_roundtrip", "sysreg_test:fpsr_roundtrip" },
    { A64_SYSREG_DAIF, "DAIF", A64_SYSREG_TIER2, A64_SYSREG_ROUTE_UNSUPPORTED,
      A64_SYSREG_ROUTE_UNSUPPORTED, "sysreg_test:unsupported_routes",
      "sysreg_test:unsupported_routes" },
    { A64_SYSREG_CTR_EL0, "CTR_EL0", A64_SYSREG_TIER1, A64_SYSREG_ROUTE_TCTI_FASTPATH,
      A64_SYSREG_ROUTE_UNSUPPORTED, "sysreg_test:ctr_el0_read", "sysreg_test:read_only_routes" },
    { A64_SYSREG_DCZID_EL0, "DCZID_EL0", A64_SYSREG_TIER1, A64_SYSREG_ROUTE_TCTI_FASTPATH,
      A64_SYSREG_ROUTE_UNSUPPORTED, "sysreg_test:dczid_el0_read", "sysreg_test:read_only_routes" },
    { A64_SYSREG_CNTFRQ_EL0, "CNTFRQ_EL0", A64_SYSREG_TIER2, A64_SYSREG_ROUTE_UNSUPPORTED,
      A64_SYSREG_ROUTE_UNSUPPORTED, "sysreg_test:unsupported_routes",
      "sysreg_test:unsupported_routes" },
    { A64_SYSREG_CNTVCT_EL0, "CNTVCT_EL0", A64_SYSREG_TIER2, A64_SYSREG_ROUTE_UNSUPPORTED,
      A64_SYSREG_ROUTE_UNSUPPORTED, "sysreg_test:unsupported_routes",
      "sysreg_test:unsupported_routes" },
};

static uint64_t a64_sysreg_source_value(struct cpu_state *cpu, uint64_t rt)
{
    if (rt >= 31)
        return 0;
    return cpu->x[rt];
}

const a64_sysreg_spec_t *a64_sysreg_lookup(uint16_t sysreg)
{
    size_t count = sizeof(a64_sysreg_specs) / sizeof(a64_sysreg_specs[0]);
    for (size_t i = 0; i < count; i++) {
        if (a64_sysreg_specs[i].encoding == sysreg)
            return &a64_sysreg_specs[i];
    }
    return NULL;
}

const char *a64_sysreg_name(uint16_t sysreg)
{
    const a64_sysreg_spec_t *spec = a64_sysreg_lookup(sysreg);
    if (spec == NULL)
        return "UNKNOWN_SYSREG";
    return spec->name;
}

enum a64_sysreg_route a64_sysreg_route_for_access(uint16_t sysreg, int is_write)
{
    const a64_sysreg_spec_t *spec = a64_sysreg_lookup(sysreg);
    if (spec == NULL)
        return A64_SYSREG_ROUTE_UNSUPPORTED;
    return is_write ? spec->write_route : spec->read_route;
}

int a64_sysreg_read(struct cpu_state *cpu, uint16_t sysreg, uint64_t rd)
{
    enum a64_sysreg_route route = a64_sysreg_route_for_access(sysreg, 0);

    if (route == A64_SYSREG_ROUTE_UNSUPPORTED)
        return TCTI_EXIT_UNSUPPORTED_SYSREG;
    if (route == A64_SYSREG_ROUTE_COMPLEX_FALLBACK)
        return TCTI_EXIT_COMPLEX;
    if (rd >= 31)
        return TCTI_EXIT_NORMAL;

    switch (sysreg) {
    case A64_SYSREG_NZCV:
        cpu->x[rd] = cpu->pstate & 0xF0000000ULL;
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_TPIDR_EL0:
    case A64_SYSREG_TPIDRRO_EL0:
        cpu->x[rd] = cpu->tpidr_el0;
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_FPCR:
        cpu->x[rd] = cpu->fpcr;
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_FPSR:
        cpu->x[rd] = cpu->fpsr;
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_CTR_EL0:
        cpu->x[rd] = A64_SYSREG_CTR_EL0_VALUE;
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_DCZID_EL0:
        cpu->x[rd] = A64_SYSREG_DCZID_EL0_VALUE;
        return TCTI_EXIT_NORMAL;
    default:
        return TCTI_EXIT_UNSUPPORTED_SYSREG;
    }
}

int a64_sysreg_write(struct cpu_state *cpu, uint16_t sysreg, uint64_t rt)
{
    uint64_t value = a64_sysreg_source_value(cpu, rt);
    enum a64_sysreg_route route = a64_sysreg_route_for_access(sysreg, 1);

    if (route == A64_SYSREG_ROUTE_UNSUPPORTED)
        return TCTI_EXIT_UNSUPPORTED_SYSREG;
    if (route == A64_SYSREG_ROUTE_COMPLEX_FALLBACK)
        return TCTI_EXIT_COMPLEX;

    switch (sysreg) {
    case A64_SYSREG_NZCV:
        cpu->pstate = (cpu->pstate & ~0xF0000000ULL) | (value & 0xF0000000ULL);
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_TPIDR_EL0:
        cpu->tpidr_el0 = value;
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_FPCR:
        cpu->fpcr = (uint32_t)value;
        return TCTI_EXIT_NORMAL;
    case A64_SYSREG_FPSR:
        cpu->fpsr = (uint32_t)value;
        return TCTI_EXIT_NORMAL;
    default:
        return TCTI_EXIT_UNSUPPORTED_SYSREG;
    }
}

int a64_sysreg_handle_complex(struct cpu_state *cpu, const a64_instr_t *instr)
{
    int exit_reason;

    if (instr->subtype == 2)
        exit_reason = a64_sysreg_read(cpu, (uint16_t)instr->sysreg, instr->Rd);
    else if (instr->subtype == 4)
        exit_reason = a64_sysreg_write(cpu, (uint16_t)instr->sysreg, instr->Rd);
    else
        return -1;

    if (exit_reason != TCTI_EXIT_NORMAL)
        return -1;

    cpu->pc += 4;
    return 0;
}
