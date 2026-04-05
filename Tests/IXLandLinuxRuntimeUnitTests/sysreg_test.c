#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name(void)
#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        printf("  Running %s... ", #name);                                                         \
        tests_run++;                                                                               \
        test_##name();                                                                             \
        tests_passed++;                                                                            \
        printf("OK\n");                                                                            \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAILED: %s at line %d\n", #cond, __LINE__);                                    \
            tests_failed++;                                                                        \
            tests_passed--;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

static struct cpu_state cpu;

static void setup_cpu(void)
{
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x4000;
}

TEST(route_matrix)
{
    const a64_sysreg_spec_t *spec = a64_sysreg_lookup(A64_SYSREG_NZCV);
    ASSERT(spec != NULL);
    ASSERT_EQ(spec->tier, A64_SYSREG_TIER0);
    ASSERT_EQ(spec->read_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);
    ASSERT_EQ(spec->write_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);

    spec = a64_sysreg_lookup(A64_SYSREG_FPCR);
    ASSERT(spec != NULL);
    ASSERT_EQ(spec->tier, A64_SYSREG_TIER1);
    ASSERT_EQ(spec->read_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);

    spec = a64_sysreg_lookup(A64_SYSREG_CTR_EL0);
    ASSERT(spec != NULL);
    ASSERT_EQ(spec->tier, A64_SYSREG_TIER1);
    ASSERT_EQ(spec->read_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);
    ASSERT_EQ(spec->write_route, A64_SYSREG_ROUTE_UNSUPPORTED);

    spec = a64_sysreg_lookup(A64_SYSREG_DCZID_EL0);
    ASSERT(spec != NULL);
    ASSERT_EQ(spec->tier, A64_SYSREG_TIER1);
    ASSERT_EQ(spec->read_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);
    ASSERT_EQ(spec->write_route, A64_SYSREG_ROUTE_UNSUPPORTED);

    spec = a64_sysreg_lookup(A64_SYSREG_DAIF);
    ASSERT(spec != NULL);
    ASSERT_EQ(spec->tier, A64_SYSREG_TIER2);
    ASSERT_EQ(spec->read_route, A64_SYSREG_ROUTE_UNSUPPORTED);
    ASSERT_EQ(spec->write_route, A64_SYSREG_ROUTE_UNSUPPORTED);
}

TEST(nzcv_roundtrip)
{
    setup_cpu();
    cpu.x[0] = 0xA0000000ULL;

    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_NZCV, 0), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.pstate & 0xF0000000ULL, 0xA0000000ULL);

    cpu.x[1] = 0;
    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_NZCV, 1), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.x[1], 0xA0000000ULL);
}

TEST(tpidr_el0_roundtrip)
{
    setup_cpu();
    cpu.x[3] = 0x123456789ABCDEF0ULL;

    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_TPIDR_EL0, 3), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.tpidr_el0, 0x123456789ABCDEF0ULL);

    cpu.x[4] = 0;
    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_TPIDR_EL0, 4), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.x[4], 0x123456789ABCDEF0ULL);
}

TEST(tpidrro_el0_read)
{
    setup_cpu();
    cpu.tpidr_el0 = 0xCAFEBABE12340000ULL;

    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_TPIDRRO_EL0, 5), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.x[5], 0xCAFEBABE12340000ULL);
    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_TPIDRRO_EL0, 5), TCTI_EXIT_UNSUPPORTED_SYSREG);
}

TEST(fpcr_roundtrip)
{
    setup_cpu();
    cpu.x[6] = 0x00C00000ULL;

    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_FPCR, 6), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.fpcr, 0x00C00000U);

    cpu.x[7] = 0;
    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_FPCR, 7), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.x[7], 0x00C00000ULL);
}

TEST(fpsr_roundtrip)
{
    setup_cpu();
    cpu.x[8] = 0x0000001FULL;

    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_FPSR, 8), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.fpsr, 0x0000001FU);

    cpu.x[9] = 0;
    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_FPSR, 9), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.x[9], 0x0000001FULL);
}

TEST(ctr_el0_read)
{
    setup_cpu();

    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_CTR_EL0, 10), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.x[10], A64_SYSREG_CTR_EL0_VALUE);
}

TEST(dczid_el0_read)
{
    setup_cpu();

    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_DCZID_EL0, 11), TCTI_EXIT_NORMAL);
    ASSERT_EQ(cpu.x[11], A64_SYSREG_DCZID_EL0_VALUE);
}

TEST(read_only_routes)
{
    setup_cpu();
    cpu.x[12] = 0xFFFF;

    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_TPIDRRO_EL0, 12), TCTI_EXIT_UNSUPPORTED_SYSREG);
    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_CTR_EL0, 12), TCTI_EXIT_UNSUPPORTED_SYSREG);
    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_DCZID_EL0, 12), TCTI_EXIT_UNSUPPORTED_SYSREG);
}

TEST(unsupported_routes)
{
    setup_cpu();
    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_DAIF, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
    ASSERT_EQ(a64_sysreg_write(&cpu, A64_SYSREG_DAIF, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_CNTFRQ_EL0, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
    ASSERT_EQ(a64_sysreg_read(&cpu, A64_SYSREG_CNTVCT_EL0, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
}

int main(void)
{
    printf("aarch64 Sysreg Unit Tests\n");
    printf("=========================\n\n");

    RUN_TEST(route_matrix);
    RUN_TEST(nzcv_roundtrip);
    RUN_TEST(tpidr_el0_roundtrip);
    RUN_TEST(tpidrro_el0_read);
    RUN_TEST(fpcr_roundtrip);
    RUN_TEST(fpsr_roundtrip);
    RUN_TEST(ctr_el0_read);
    RUN_TEST(dczid_el0_read);
    RUN_TEST(read_only_routes);
    RUN_TEST(unsupported_routes);

    printf("\n=========================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
