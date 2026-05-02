#include "tcti_harness_truth.h"

#include <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#include <IXLandLinuxRuntime/emu/aarch64/decode.h>
#include <IXLandLinuxRuntime/emu/aarch64/memory.h>
#include <IXLandLinuxRuntime/emu/tlb.h>
#include <IXLandLinuxRuntime/kernel/memory.h>

#include <string.h>

typedef void (*tcti_gadget_t)(void);

#define A64_MAX_GADGETS_PER_BLOCK 512
#define A64_GEN_OK 0

typedef struct a64_gen_state {
    tcti_gadget_t *gadgets;
    size_t max_gadgets;
    size_t num_gadgets;
    uint64_t guest_pc;
    uint32_t raw_insn;
    a64_instr_t decoded;
    uint64_t start_pc;
    uint64_t end_pc;
    int is_complete;
    int instructions_processed;
    int conservative_mode;
} a64_gen_state_t;

extern int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max);
extern void a64_gen_reset(a64_gen_state_t *state, uint64_t pc);
extern int a64_gen_instruction(a64_gen_state_t *state, uint32_t insn, uint64_t pc);
extern int a64_gen_finalize(a64_gen_state_t *state);

extern const tcti_gadget_t gadget_add_reg[16][16][16];
extern const tcti_gadget_t gadget_mov_reg[16][16];
extern const tcti_gadget_t gadget_bcond[16];
extern tcti_gadget_t gadget_addsub_imm_fallback;
extern tcti_gadget_t gadget_addsub_reg_fallback;
extern tcti_gadget_t gadget_ccmp_fallback;
extern tcti_gadget_t gadget_csel_fallback;
extern tcti_gadget_t gadget_extend_x14;
extern tcti_gadget_t gadget_ldr_x;
extern tcti_gadget_t gadget_logical_reg_fallback;
extern tcti_gadget_t gadget_str_x;
extern void tcti_entry_block(void **gadgets, struct cpu_state *cpu);
extern void tcti_exit_block(int reason);

static int tcti_harness_run_generated_block(struct cpu_state *cpu, uint64_t pc,
                                            const uint32_t *insns, size_t count);

void tcti_harness_single_gadget_snapshot(void (*gadget)(void), const uint64_t *in_regs,
                                         uint64_t *out_regs);

__asm__(".text\n"
        ".align 2\n"
        ".global _tcti_harness_single_gadget_snapshot\n"
        "_tcti_harness_single_gadget_snapshot:\n"
        "stp x19, x20, [sp, #-16]!\n"
        "stp x21, x22, [sp, #-16]!\n"
        "stp x23, x24, [sp, #-16]!\n"
        "stp x25, x26, [sp, #-16]!\n"
        "stp x27, x28, [sp, #-16]!\n"
        "stp x29, x30, [sp, #-16]!\n"
        "mov x29, sp\n"
        "mov x19, x0\n"
        "mov x21, x1\n"
        "mov x20, x2\n"
        "sub sp, sp, #16\n"
        "mov x28, sp\n"
        "adr x22, 2f\n"
        "str x22, [x28]\n"
        "str x20, [x28, #8]\n"
        "ldr x1, [x21, #0]\n"
        "ldr x2, [x21, #8]\n"
        "ldr x3, [x21, #16]\n"
        "ldr x4, [x21, #24]\n"
        "ldr x5, [x21, #32]\n"
        "ldr x6, [x21, #40]\n"
        "ldr x7, [x21, #48]\n"
        "ldr x8, [x21, #56]\n"
        "ldr x9, [x21, #64]\n"
        "ldr x10, [x21, #72]\n"
        "ldr x11, [x21, #80]\n"
        "ldr x12, [x21, #88]\n"
        "ldr x13, [x21, #96]\n"
        "ldr x14, [x21, #104]\n"
        "ldr x15, [x21, #112]\n"
        "ldr x16, [x21, #120]\n"
        "br x19\n"
        "2:\n"
        "ldr x20, [x28], #8\n"
        "str x1, [x20, #0]\n"
        "str x2, [x20, #8]\n"
        "str x3, [x20, #16]\n"
        "str x4, [x20, #24]\n"
        "str x5, [x20, #32]\n"
        "str x6, [x20, #40]\n"
        "str x7, [x20, #48]\n"
        "str x8, [x20, #56]\n"
        "str x9, [x20, #64]\n"
        "str x10, [x20, #72]\n"
        "str x11, [x20, #80]\n"
        "str x12, [x20, #88]\n"
        "str x13, [x20, #96]\n"
        "str x14, [x20, #104]\n"
        "str x15, [x20, #112]\n"
        "str x16, [x20, #120]\n"
        "str x27, [x20, #128]\n"
        "str x28, [x20, #136]\n"
        "str x29, [x20, #144]\n"
        "str x30, [x20, #152]\n"
        "add sp, sp, #16\n"
        "ldp x29, x30, [sp], #16\n"
        "ldp x27, x28, [sp], #16\n"
        "ldp x25, x26, [sp], #16\n"
        "ldp x23, x24, [sp], #16\n"
        "ldp x21, x22, [sp], #16\n"
        "ldp x19, x20, [sp], #16\n"
        "ret\n");

void tcti_harness_seed_inputs(uint64_t regs[TCTI_HARNESS_INPUT_REGS])
{
    for (int i = 0; i < TCTI_HARNESS_INPUT_REGS; i++)
        regs[i] = 0xA500000000000000ULL + (uint64_t)i;
}

void tcti_harness_run_snapshot(void (*gadget)(void),
                               const uint64_t in_regs[TCTI_HARNESS_INPUT_REGS],
                               uint64_t out_regs[TCTI_HARNESS_SNAPSHOT_REGS])
{
    memset(out_regs, 0, sizeof(uint64_t) * TCTI_HARNESS_SNAPSHOT_REGS);
    tcti_harness_single_gadget_snapshot(gadget, in_regs, out_regs);
}

int tcti_harness_case_add_7_13_14(tcti_harness_snapshot_t *snapshot)
{
    uint64_t lhs = 0xFFFFFEF7FBF0ULL;
    uint64_t rhs = 0x8ULL;

    tcti_harness_seed_inputs(snapshot->in_regs);
    snapshot->in_regs[0] = 0x1111111111111111ULL;
    snapshot->in_regs[13] = lhs; // guest x13 -> host x14
    snapshot->in_regs[14] = rhs; // guest x14 -> host x15

    tcti_harness_run_snapshot(gadget_add_reg[7][13][14], snapshot->in_regs, snapshot->out_regs);

    // guest x7 (dest) -> host x8 (out_regs[7]), guest x13 -> host x14 (out_regs[13]), guest x14 ->
    // host x15 (out_regs[14])
    return snapshot->out_regs[7] == (lhs + rhs) && snapshot->out_regs[13] == lhs &&
           snapshot->out_regs[14] == rhs && snapshot->out_regs[0] == 0x1111111111111111ULL;
}

int tcti_harness_case_add_0_1_2(tcti_harness_snapshot_t *snapshot)
{
    tcti_harness_seed_inputs(snapshot->in_regs);
    snapshot->in_regs[1] = 0x40ULL;               // guest x1 -> host x2
    snapshot->in_regs[2] = 0x2ULL;                // guest x2 -> host x3
    snapshot->in_regs[8] = 0x3333333333333333ULL; // guest x8 -> host x9

    tcti_harness_run_snapshot(gadget_add_reg[0][1][2], snapshot->in_regs, snapshot->out_regs);

    // ADD x0, x1, x2: guest x0 (host x1) = guest x1 (host x2) + guest x2 (host x3)
    return snapshot->out_regs[0] == 0x42ULL && snapshot->out_regs[1] == 0x40ULL &&
           snapshot->out_regs[2] == 0x2ULL && snapshot->out_regs[8] == 0x3333333333333333ULL;
}

int tcti_harness_case_mov_2_7(tcti_harness_snapshot_t *snapshot)
{
    tcti_harness_seed_inputs(snapshot->in_regs);
    snapshot->in_regs[7] = 0x123456789ABCDEF0ULL; // guest x7 (source) -> host x8
    snapshot->in_regs[0] = 0x4444444444444444ULL; // guest x0 -> host x1
    snapshot->in_regs[2] = 0;                     // guest x2 (dest) -> host x3

    tcti_harness_run_snapshot(gadget_mov_reg[2][7], snapshot->in_regs, snapshot->out_regs);

    // MOV x2, x7: guest x2 (host x3) = guest x7 (host x8)
    return snapshot->out_regs[2] == 0x123456789ABCDEF0ULL &&
           snapshot->out_regs[7] == 0x123456789ABCDEF0ULL &&
           snapshot->out_regs[0] == 0x4444444444444444ULL;
}

uint64_t tcti_harness_case_entry_restores_pstate_for_bcond_ne(void)
{
    enum {
        A64_COND_NE = 1,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0; // Z clear, so B.NE must take the target path.

    void *gadgets[] = {
        (void *)gadget_bcond[A64_COND_NE],
        (void *)0x2000,
        (void *)0x1004,
    };

    /*
     * Force host NZCV to Z=1 immediately before entering TCTI. The branch
     * result must still follow cpu.pstate (Z=0), proving block entry restores
     * guest flags after returning from C/syscall/fault handling.
     */
    __asm__ volatile("cmp xzr, xzr\n\t"
                     "mov x0, %[gadgets]\n\t"
                     "mov x1, %[cpu]\n\t"
                     "bl _tcti_entry_block\n\t"
                     :
                     : [gadgets] "r"(gadgets), [cpu] "r"(&cpu)
                     : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
                       "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30",
                       "cc", "memory");

    return cpu.pc;
}

uint64_t tcti_harness_case_cmp_w20_ccmp_gt_bls_uses_32bit_flags(void)
{
    enum {
        alloc_bls_target = 0x29c50,
        alloc_bls_fallthrough = 0x29bd4,
    };

    void *gadgets[] = {
        (void *)gadget_addsub_imm_fallback,
        (void *)31, // rd: XZR/WZR
        (void *)20, // rn: W20
        (void *)0,  // imm
        (void *)1,  // is_sub
        (void *)1,  // set_flags
        (void *)0,  // is_64bit
        (void *)0,  // rd_is_sp
        (void *)0,  // rn_is_sp
        (void *)gadget_ccmp_fallback,
        (void *)25,               // rn
        (void *)21,               // rm
        (void *)0,                // imm_operand
        (void *)A64_GT,           // cond
        (void *)0,                // nzcv false-condition immediate
        (void *)A64_DP_REG_CCMP,  // subtype
        (void *)1,                // is_64bit
        (void *)gadget_bcond[A64_LS],
        (void *)alloc_bls_target,
        (void *)alloc_bls_fallthrough,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[20] = UINT64_MAX;
    cpu.x[21] = 0x5663170f;
    cpu.x[25] = 0x56631720;

    tcti_entry_block(gadgets, &cpu);

    return cpu.pc == alloc_bls_target ? cpu.pstate : cpu.pc;
}

uint64_t tcti_harness_case_cmp_w2_w1_uxtb_csel_uses_w_width(void)
{
    enum {
        branch_target = 0x1000,
    };

    void *gadgets[] = {
        (void *)gadget_mov_reg[13][1],
        (void *)gadget_extend_x14,
        (void *)A64_EXT_UXTB,
        (void *)gadget_addsub_reg_fallback,
        (void *)31, // rd: WZR
        (void *)2,  // rn: W2
        (void *)13, // rm: extended W1 in x14
        (void *)A64_SHIFT_LSL,
        (void *)0, // imm_shift
        (void *)1, // is_sub
        (void *)1, // set_flags
        (void *)0, // is_64bit
        (void *)gadget_csel_fallback,
        (void *)0,             // rd
        (void *)0,             // rn
        (void *)31,            // rm: XZR
        (void *)A64_EQ,        // cond
        (void *)0,             // subtype: CSEL
        (void *)1,             // is_64bit
        (void *)gadget_bcond[A64_AL],
        (void *)branch_target,
        (void *)0,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0x123456789abcdef0ULL;
    cpu.x[1] = 0x2e;
    cpu.x[2] = 0xffffffff0000002eULL;

    tcti_entry_block(gadgets, &cpu);

    return cpu.x[0];
}

uint64_t tcti_harness_case_csel_eq_selects_true_operand(void)
{
    enum {
        branch_target = 0x1000,
    };

    void *gadgets[] = {
        (void *)gadget_csel_fallback,
        (void *)0,      // rd
        (void *)0,      // rn
        (void *)31,     // rm: XZR
        (void *)A64_EQ, // cond
        (void *)0,      // subtype: CSEL
        (void *)1,      // is_64bit
        (void *)gadget_bcond[A64_AL],
        (void *)branch_target,
        (void *)0,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0x60000000ULL; // Z=1, C=1: EQ must hold.
    cpu.x[0] = 0x123456789abcdef0ULL;

    tcti_entry_block(gadgets, &cpu);

    return cpu.x[0];
}

uint64_t tcti_harness_case_csel_preserves_flags_for_bcond(void)
{
    enum {
        branch_target = 0x2000,
        fallthrough = 0x1004,
    };

    void *gadgets[] = {
        (void *)gadget_csel_fallback,
        (void *)3,      // rd
        (void *)0,      // rn
        (void *)31,     // rm: XZR
        (void *)A64_EQ, // cond
        (void *)0,      // subtype: CSEL
        (void *)1,      // is_64bit
        (void *)gadget_bcond[A64_EQ],
        (void *)branch_target,
        (void *)fallthrough,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0x60000000ULL; // Z=1, C=1: EQ must hold across CSEL.
    cpu.x[0] = 0x123456789abcdef0ULL;

    tcti_entry_block(gadgets, &cpu);

    return cpu.pc;
}

uint64_t tcti_harness_case_cmp_add_csel_ne_uses_preserved_zero_flag(void)
{
    enum {
        stack_top = 0x200000,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.sp = stack_top;
    cpu.x[2] = 0x1111111111111111ULL;
    cpu.x[4] = 0;

    static const uint32_t insns[] = {
        0xf100009f, // cmp x4, #0
        0x910103e2, // add x2, sp, #0x40
        0x9a9f1042, // csel x2, x2, xzr, ne
        0xd28010c8, // mov x8, #134
        0xd2800103, // mov x3, #8
        0xd4000001, // svc #0
    };

    if (tcti_harness_run_generated_block(&cpu, 0x51f1c, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0) {
        return UINT64_MAX;
    }

    return cpu.x[2];
}

uint64_t tcti_harness_case_cmp_ccmp_false_immediate_clears_zero(void)
{
    static const uint32_t insns[] = {
        0xf100009f, // cmp x4, #0
        0x7a401800, // ccmp w0, #0, #0, ne
        0x54000040, // b.eq +8
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0;
    cpu.x[4] = 0;

    if (tcti_harness_run_generated_block(&cpu, 0x51f38, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0) {
        return UINT64_MAX;
    }

    return cpu.pc;
}

uint64_t tcti_harness_case_strchrnul_vector_mask_finds_dot(void)
{
    enum {
        branch_target = 0x1000,
    };

    void *gadgets[] = {
        (void *)gadget_logical_reg_fallback,
        (void *)5,             // rd
        (void *)3,             // rn
        (void *)6,             // rm
        (void *)A64_SHIFT_LSL, // shift_type
        (void *)0,             // imm_shift
        (void *)2,             // subtype: EOR
        (void *)0,             // set_flags
        (void *)1,             // is_64bit
        (void *)gadget_logical_reg_fallback,
        (void *)4,             // rd
        (void *)3,             // rn
        (void *)6,             // rm
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)6, // subtype: EON
        (void *)0,
        (void *)1,
        (void *)gadget_add_reg[5][5][7],
        (void *)gadget_add_reg[0][3][7],
        (void *)gadget_logical_reg_fallback,
        (void *)4,             // rd
        (void *)4,             // rn
        (void *)5,             // rm
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)0, // subtype: AND
        (void *)0,
        (void *)1,
        (void *)gadget_logical_reg_fallback,
        (void *)0,             // rd
        (void *)0,             // rn
        (void *)3,             // rm
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)4, // subtype: BIC
        (void *)0,
        (void *)1,
        (void *)gadget_logical_reg_fallback,
        (void *)0,             // rd
        (void *)0,             // rn
        (void *)4,             // rm
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)1, // subtype: ORR
        (void *)0,
        (void *)1,
        (void *)gadget_bcond[A64_AL],
        (void *)branch_target,
        (void *)0,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[3] = 0x6165726874702e63ULL; // "c.pthrea" little-endian, includes '.'.
    cpu.x[6] = 0x2e2e2e2e2e2e2e2eULL;
    cpu.x[7] = 0xfefefefefefefeffULL;

    tcti_entry_block(gadgets, &cpu);

    return cpu.x[0] & 0x8080808080808080ULL;
}

uint64_t tcti_harness_case_logical_mov_roundtrips_memory_backed_x19(void)
{
    enum {
        A64_LOGICAL_ORR = 1,
    };

    void *gadgets[] = {
        (void *)gadget_logical_reg_fallback,
        (void *)19,              // rd: x19, memory-backed
        (void *)31,              // rn: xzr for MOV alias
        (void *)3,               // rm: x3
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)A64_LOGICAL_ORR,
        (void *)0,
        (void *)1,
        (void *)gadget_logical_reg_fallback,
        (void *)5,               // rd: x5, hot
        (void *)31,              // rn: xzr for MOV alias
        (void *)19,              // rm: x19, memory-backed
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)A64_LOGICAL_ORR,
        (void *)0,
        (void *)1,
        (void *)tcti_exit_block,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[3] = 3;
    cpu.x[5] = 0xaaaabfff5555ffffULL;
    cpu.x[19] = 0x1111222233334444ULL;

    tcti_entry_block(gadgets, &cpu);

    return (cpu.x[19] == 3 && cpu.x[5] == 3) ? 0 : cpu.x[5];
}

uint64_t tcti_harness_case_cset_eq_then_add_to_x3(void)
{
    enum {
        A64_CSEL_CSINC = 1,
    };

    void *gadgets[] = {
        (void *)gadget_csel_fallback,
        (void *)3,               // rd
        (void *)31,              // rn: xzr
        (void *)31,              // rm: xzr
        (void *)A64_NE,          // cset eq is csinc x3,xzr,xzr,ne
        (void *)A64_CSEL_CSINC,
        (void *)1,
        (void *)gadget_addsub_imm_fallback,
        (void *)3,               // rd
        (void *)3,               // rn
        (void *)2,
        (void *)0,
        (void *)0,
        (void *)1,
        (void *)0,
        (void *)0,
        (void *)tcti_exit_block,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0x40000000ULL; // Z set, so EQ true and NE false.
    cpu.x[3] = 0xaaaabfff5555ffffULL;

    tcti_entry_block(gadgets, &cpu);

    return cpu.x[3];
}

uint64_t tcti_harness_case_ldr_x5_from_memory_backed_x27(void)
{
    enum {
        guest_addr = 0x100000,
        expected = 0x0123456789abcdefULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(guest_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[5] = 0xaaaabfff5555ffffULL;
    cpu.x[27] = guest_addr;

    if (a64_guest_write64(&cpu, &tlb, guest_addr, expected) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    void *gadgets[] = {
        (void *)gadget_ldr_x,
        (void *)0x6a4e8,
        (void *)5,
        (void *)27,
        (void *)0,
        (void *)A64_SIZE_X,
        (void *)A64_INDEX_OFFSET,
        (void *)0,
        (void *)tcti_exit_block,
    };

    tcti_entry_block(gadgets, &cpu);

    uint64_t result = cpu.x[5];
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_str_x0_to_memory_backed_x22_scaled_x1(void)
{
    enum {
        guest_addr = 0x110000,
        expected = 0xfedcba9876543210ULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(guest_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = expected;
    cpu.x[1] = 2;
    cpu.x[22] = guest_addr;

    uint64_t meta = (1ULL << 8) | (1ULL << 16) | ((uint64_t)A64_EXT_LSL << 24) |
                    ((uint64_t)A64_SIZE_X << 32);
    void *gadgets[] = {
        (void *)gadget_str_x,
        (void *)0x6af98,
        (void *)0,
        (void *)22,
        (void *)0,
        (void *)A64_SIZE_X,
        (void *)A64_INDEX_OFFSET,
        (void *)meta,
        (void *)tcti_exit_block,
    };

    tcti_entry_block(gadgets, &cpu);

    uint64_t result = 0;
    (void)a64_guest_read64(&cpu, &tlb, guest_addr + 16, &result);
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_relocation_loop_preserves_loaded_x5(void)
{
    enum {
        base_ptr = 0x120000,
        rela_ptr = 0x121000,
        main_base = 0x56555000,
        relocation_offset = 0x00000000000dfee8ULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_ptr), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[2] = 0x1600000401ULL; // relocation info: high symbol bits plus low type bits.
    cpu.x[4] = 0x1111111111111111ULL;
    cpu.x[5] = 0xaaaabfff5555ffffULL;
    cpu.x[19] = 2;
    cpu.x[26] = base_ptr;
    cpu.x[27] = rela_ptr;

    if (a64_guest_write64(&cpu, &tlb, base_ptr, main_base) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, rela_ptr, relocation_offset) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK] = {};
    a64_gen_state_t gen;
    if (a64_gen_init(&gen, gadgets, A64_MAX_GADGETS_PER_BLOCK) != A64_GEN_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }
    a64_gen_reset(&gen, 0x6a4dc);

    static const uint32_t first_block[] = {
        0x7200785c, // ands w28, w2, #0x7fffffff
        0x54ffff40, // b.eq 0x6a4c8
    };
    static const uint32_t second_block[] = {
        0xf9400344, // ldr x4, [x26]
        0xf9400365, // ldr x5, [x27]
        0x8b05008c, // add x12, x4, x5
        0xf1000e7f, // cmp x19, #3
        0x54ffe101, // b.ne 0x6a114
    };

    for (size_t i = 0; i < sizeof(first_block) / sizeof(first_block[0]); i++) {
        int ret = a64_gen_instruction(&gen, first_block[i], gen.guest_pc);
        if (ret != A64_GEN_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 3;
        }
        if (gen.is_complete)
            break;
        gen.guest_pc += 4;
    }

    tcti_entry_block((void **)gadgets, &cpu);
    if (cpu.pc != 0x6a4e4) {
        uint64_t result = 0xbad0000000000000ULL | cpu.pc;
        mem_destroy(&mem);
        return result;
    }

    memset(gadgets, 0, sizeof(gadgets));
    a64_gen_reset(&gen, cpu.pc);
    for (size_t i = 0; i < sizeof(second_block) / sizeof(second_block[0]); i++) {
        int ret = a64_gen_instruction(&gen, second_block[i], gen.guest_pc);
        if (ret != A64_GEN_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 4;
        }
        if (gen.is_complete)
            break;
        gen.guest_pc += 4;
    }

    tcti_entry_block((void **)gadgets, &cpu);

    uint64_t result = cpu.x[5];
    mem_destroy(&mem);
    return result;
}

static int tcti_harness_run_generated_block(struct cpu_state *cpu, uint64_t pc,
                                            const uint32_t *insns, size_t count)
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK] = {};
    a64_gen_state_t gen;
    if (a64_gen_init(&gen, gadgets, A64_MAX_GADGETS_PER_BLOCK) != A64_GEN_OK)
        return -1;
    a64_gen_reset(&gen, pc);
    for (size_t i = 0; i < count; i++) {
        int ret = a64_gen_instruction(&gen, insns[i], gen.guest_pc);
        if (ret != A64_GEN_OK)
            return ret;
        if (gen.is_complete)
            break;
        gen.guest_pc += 4;
    }
    if (!gen.is_complete && a64_gen_finalize(&gen) != A64_GEN_OK)
        return -3;
    tcti_entry_block((void **)gadgets, cpu);
    return 0;
}

uint64_t tcti_harness_case_add_hot_pair_to_memory_backed_x23(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x5655ab48ULL;
    cpu.x[2] = 0xc8ULL;
    cpu.x[23] = 0x1111111111111111ULL;

    static const uint32_t insns[] = {
        0x8b020037, // add x23, x1, x2
    };

    if (tcti_harness_run_generated_block(&cpu, 0x6a0c8, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0)
        return UINT64_MAX;

    return cpu.x[23];
}

uint64_t tcti_harness_case_cmp_memory_backed_x27_x23_branches_eq(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[23] = 0x5655ab48ULL;
    cpu.x[27] = 0x5655ab48ULL;

    static const uint32_t insns[] = {
        0xeb17037f, // cmp x27, x23
        0x54000e00, // b.eq 0x6a690
    };

    if (tcti_harness_run_generated_block(&cpu, 0x6a4cc, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0)
        return UINT64_MAX;

    return cpu.pc;
}

uint64_t tcti_harness_case_stack_pair_roundtrips_hot_x5_x4(void)
{
    enum {
        stack_ptr = 0x130000,
        x5_value = 0x00000000000dfee8ULL,
        x4_value = 0x0000000056555000ULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.sp = stack_ptr;
    cpu.x[5] = x5_value;
    cpu.x[4] = x4_value;

    static const uint32_t store_pair[] = {
        0xa90913e5, // stp x5, x4, [sp, #0x90]
    };
    if (tcti_harness_run_generated_block(&cpu, 0x6a298, store_pair,
                                         sizeof(store_pair) / sizeof(store_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    cpu.x[5] = 0xaaaabfff5555ffffULL;
    cpu.x[4] = 0x1111111111111111ULL;

    static const uint32_t load_pair[] = {
        0xa94913e5, // ldp x5, x4, [sp, #0x90]
    };
    if (tcti_harness_run_generated_block(&cpu, 0x6a2ac, load_pair,
                                         sizeof(load_pair) / sizeof(load_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = ((cpu.x[4] == x4_value) ? 0 : 1);
    result |= ((cpu.x[5] == x5_value) ? 0 : 2);

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_dynamic_tag_scaled_store_uses_full_index(void)
{
    enum {
        stack_ptr = 0x130000,
        slot_base = stack_ptr + 0x48,
        relr_size_tag = 35,
        relr_addr_tag = 36,
        wrong_rel_tag = 17,
        wrong_rel_size_tag = 18,
        relr_size = 0xc8,
        relr_value = 0x5b48,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = relr_size;
    cpu.x[1] = relr_size_tag;
    cpu.x[22] = slot_base;

    static const uint32_t store_dynamic_tag[] = {
        0xf8217ac0, // str x0, [x22, x1, lsl #3]
    };
    if (tcti_harness_run_generated_block(&cpu, 0x6af98, store_dynamic_tag,
                                         sizeof(store_dynamic_tag) /
                                             sizeof(store_dynamic_tag[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    cpu.x[0] = relr_value;
    cpu.x[1] = relr_addr_tag;
    if (tcti_harness_run_generated_block(&cpu, 0x6af98, store_dynamic_tag,
                                         sizeof(store_dynamic_tag) /
                                             sizeof(store_dynamic_tag[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t correct_size_slot = 0;
    uint64_t correct_slot = 0;
    uint64_t wrong_size_slot = 0;
    uint64_t wrong_slot = 0;
    if (a64_guest_read64(&cpu, &tlb, slot_base + relr_size_tag * 8, &correct_size_slot) !=
            A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, slot_base + relr_addr_tag * 8, &correct_slot) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, slot_base + wrong_rel_size_tag * 8, &wrong_size_slot) !=
            A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, slot_base + wrong_rel_tag * 8, &wrong_slot) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 3;
    }

    uint64_t result = 0;
    if (correct_size_slot != relr_size)
        result |= 4;
    if (correct_slot != relr_value)
        result |= 1;
    if (wrong_size_slot != 0)
        result |= 8;
    if (wrong_slot != 0)
        result |= 2;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_musl_ubfiz_symbol_index_preserves_shifted_bits(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[4] = 0xaaaaaaaaaaaaaaaaULL;
    cpu.x[6] = 0xffffffffULL;

    static const uint32_t insns[] = {
        0xd37d7cc4, // ubfiz x4, x6, #3, #32
    };

    if (tcti_harness_run_generated_block(&cpu, 0x698c0, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0)
        return UINT64_MAX;

    return cpu.x[4];
}

uint64_t tcti_harness_case_lsr_alias_uses_top_mask_not_rotate(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x8000000000000001ULL;

    static const uint32_t insns[] = {
        0xd341fc21, // lsr x1, x1, #1
    };

    if (tcti_harness_run_generated_block(&cpu, 0x6a79c, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0)
        return UINT64_MAX;

    return cpu.x[1];
}

uint64_t tcti_harness_case_musl_relr_loop_terminates_at_table_end(void)
{
    enum {
        text_base = 0x6a784,
        text_end = 0x6a7f0,
        interp_base = 0x1000,
        stack_ptr = 0x130000,
        relr_table = 0x14518,
        relr_size = 0x30,
        first_bitmap_base = 0xc0b08,
        expected_final_base = 0xc14e0,
        data_base = 0xc0000,
        data_pages = 4,
    };

    static const uint32_t relr_loop_insns[] = {
        0xf94117e7, // ldr x7, [sp, #0x228]
        0xf94113e6, // ldr x6, [sp, #0x220]
        0x8b070047, // add x7, x2, x7
        0x8b0600e7, // add x7, x7, x6
        0x1400000b, // b 0x6a7c0
        0x91002063, // add x3, x3, #0x8
        0xd341fc21, // lsr x1, x1, #1
        0xb40000c1, // cbz x1, 0x6a7b8
        0x3607ffa1, // tbz w1, #0, 0x6a798
        0xf9400065, // ldr x5, [x3]
        0x8b0200a5, // add x5, x5, x2
        0xf9000065, // str x5, [x3]
        0x17fffff9, // b 0x6a798
        0x9107e084, // add x4, x4, #0x1f8
        0xd10020c6, // sub x6, x6, #0x8
        0xb4000166, // cbz x6, 0x6a7ec
        0xcb0600e1, // sub x1, x7, x6
        0xaa0403e3, // mov x3, x4
        0xf9400021, // ldr x1, [x1]
        0x3707fe61, // tbnz w1, #0, 0x6a79c
        0xf8616843, // ldr x3, [x2, x1]
        0x8b010044, // add x4, x2, x1
        0x91002084, // add x4, x4, #0x8
        0x8b020063, // add x3, x3, x2
        0xf8216843, // str x3, [x2, x1]
        0x17fffff5, // b 0x6a7bc
    };

    static const uint64_t relr_entries[] = {
        0x00000000000bfb00ULL,
        0xaaaaae7ffffff041ULL,
        0x0000000000003f95ULL,
        0x8c7c000002090001ULL,
        0x11000068440001c1ULL,
        0x000000005e000039ULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(relr_table), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(data_base), data_pages, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_base;
    cpu.sp = stack_ptr;
    cpu.x[2] = interp_base;
    cpu.x[4] = first_bitmap_base;

    if (a64_guest_write64(&cpu, &tlb, stack_ptr + 0x228, relr_table - interp_base) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_ptr + 0x220, relr_size) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (size_t i = 0; i < sizeof(relr_entries) / sizeof(relr_entries[0]); i++) {
        if (a64_guest_write64(&cpu, &tlb, relr_table + i * sizeof(uint64_t), relr_entries[i]) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
    }

    for (uint64_t addr = data_base; addr < data_base + data_pages * 0x1000ULL; addr += 8) {
        if (a64_guest_write64(&cpu, &tlb, addr, addr - interp_base) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 3;
        }
    }

    for (unsigned step = 0; step < 4096; step++) {
        if (cpu.pc == 0x6a7ec)
            break;
        if (cpu.pc < text_base || cpu.pc >= text_end) {
            uint64_t result = 0x1000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }
        if (cpu.x[3] >= data_base + data_pages * 0x1000ULL) {
            uint64_t result = 0x2000000000000000ULL | (cpu.x[3] & 0x0fffffffffffffffULL);
            mem_destroy(&mem);
            return result;
        }

        size_t index = (size_t)((cpu.pc - text_base) / 4);
        uint64_t old_pc = cpu.pc;
        int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &relr_loop_insns[index], 1);
        if (run_ret < 0) {
            uint64_t result = 0x3000000000000000ULL | ((uint64_t)(-run_ret) << 48) | old_pc;
            mem_destroy(&mem);
            return result;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t result = 0;
    if (cpu.pc != 0x6a7ec)
        result = 0x4000000000000000ULL | ((cpu.x[6] & 0xff) << 48) |
                 ((cpu.x[1] & 0xffff) << 32) | (cpu.x[3] & 0xffffffffULL);
    else if (cpu.x[6] != 0)
        result = 0x5000000000000000ULL | cpu.x[6];
    else if (cpu.x[4] != expected_final_base)
        result = 0x6000000000000000ULL | cpu.x[4];

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_pltrel_rela_stride_selector(void)
{
    enum {
        data_ptr = 0x120000,
        loaded_value = 0x56555000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(data_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = 7;
    cpu.x[21] = data_ptr;
    cpu.x[3] = 0x1111111111111111ULL;

    if (a64_guest_write64(&cpu, &tlb, data_ptr, loaded_value) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xf1001c1f, // cmp x0, #7
        0xf94002a1, // ldr x1, [x21]
        0x9a9f17e3, // cset x3, eq
        0x91000863, // add x3, x3, #2
    };

    if (tcti_harness_run_generated_block(&cpu, 0x6afa8, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = cpu.x[3];
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_relocation_fault_path_uses_loaded_x5(void)
{
    enum {
        base_ptr = 0x120000,
        rela_ptr = 0x121000,
        stack_ptr = 0x130000,
        main_base = 0x56555000,
        relocation_offset = 0x00000000000dfee8ULL,
        relocated_value = 0x1122334455667788ULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_ptr), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(main_base + relocation_offset), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.sp = stack_ptr;
    cpu.x[2] = 0x1600000403ULL;
    cpu.x[4] = 0x1111111111111111ULL;
    cpu.x[5] = 0xaaaabfff5555ffffULL;
    cpu.x[19] = 2;
    cpu.x[26] = base_ptr;
    cpu.x[27] = rela_ptr;
    cpu.x[28] = 0x403;

    if (a64_guest_write64(&cpu, &tlb, base_ptr, main_base) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, rela_ptr, relocation_offset) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, main_base + relocation_offset, relocated_value) !=
            A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t first_block[] = {
        0x7200785c, // ands w28, w2, #0x7fffffff
        0x54ffff40, // b.eq 0x6a4c8
    };
    static const uint32_t second_block[] = {
        0xf9400344, // ldr x4, [x26]
        0xf9400365, // ldr x5, [x27]
        0x8b05008c, // add x12, x4, x5
        0xf1000e7f, // cmp x19, #3
        0x54ffe101, // b.ne 0x6a114
    };
    static const uint32_t third_block[] = {
        0x51100380, // sub w0, w28, #0x400
        0x7100081f, // cmp w0, #2
        0x54000128, // b.hi 0x6a140
    };
    static const uint32_t fourth_block[] = {
        0xb940a7e0, // ldr w0, [sp, #0xa4]
        0x34000380, // cbz w0, 0x6a1b4
    };
    static const uint32_t fifth_block[] = {
        0xf8656895, // ldr x21, [x4, x5]
    };

    (void)first_block;
    (void)second_block;
    (void)third_block;
    (void)fourth_block;
    (void)fifth_block;

    static const struct {
        uint64_t pc;
        uint32_t insn;
    } single_step_path[] = {
        { 0x6a4dc, 0x7200785c }, // ands w28, w2, #0x7fffffff
        { 0x6a4e0, 0x54ffff40 }, // b.eq 0x6a4c8
        { 0x6a4e4, 0xf9400344 }, // ldr x4, [x26]
        { 0x6a4e8, 0xf9400365 }, // ldr x5, [x27]
        { 0x6a4ec, 0x8b05008c }, // add x12, x4, x5
        { 0x6a4f0, 0xf1000e7f }, // cmp x19, #3
        { 0x6a4f4, 0x54ffe101 }, // b.ne 0x6a114
        { 0x6a114, 0x51100380 }, // sub w0, w28, #0x400
        { 0x6a118, 0x7100081f }, // cmp w0, #2
        { 0x6a11c, 0x54000128 }, // b.hi 0x6a140
        { 0x6a140, 0xb940a7e0 }, // ldr w0, [sp, #0xa4]
        { 0x6a144, 0x34000380 }, // cbz w0, 0x6a1b4
        { 0x6a1b4, 0xf8656895 }, // ldr x21, [x4, x5]
    };

    for (size_t i = 0; i < sizeof(single_step_path) / sizeof(single_step_path[0]); i++) {
        if (tcti_harness_run_generated_block(&cpu, single_step_path[i].pc,
                                             &single_step_path[i].insn, 1) < 0) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
        if (i >= 3 && cpu.x[5] != relocation_offset) {
            uint64_t result = 0xbad5000000000000ULL | (i << 48) | (cpu.x[5] & 0xffffffffffffULL);
            mem_destroy(&mem);
            return result;
        }
    }

    uint64_t result = cpu.x[21];
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_musl_snprintf_file_wpos_init(void)
{
    enum {
        stack_top = 0x130000,
        stack_ptr = stack_top - 0x100,
        expected_wpos = stack_top,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.sp = stack_top;

    static const uint32_t init_file[] = {
        0xa9b07bfd, // stp x29, x30, [sp, #-0x100]!
        0x910003fd, // mov x29, sp
        0xa90d0fe2, // stp x2, x3, [sp, #0xd0]
        0x910403e2, // add x2, sp, #0x100
        0xa9030be2, // stp x2, x2, [sp, #0x30]
        0x910343e2, // add x2, sp, #0xd0
        0x3dc00fff, // ldr q31, [sp, #0x30]
        0xf90023e2, // str x2, [sp, #0x40]
        0x128005e2, // mov w2, #-0x30
        0xb9004be2, // str w2, [sp, #0x48]
        0x12800fe2, // mov w2, #-0x80
        0xb9004fe2, // str w2, [sp, #0x4c]
        0x3d8007ff, // str q31, [sp, #0x10]
        0x910043e2, // add x2, sp, #0x10
        0x3dc013ff, // ldr q31, [sp, #0x40]
        0xa90e17e4, // stp x4, x5, [sp, #0xe0]
        0xa90f1fe6, // stp x6, x7, [sp, #0xf0]
        0xad0287e0, // stp q0, q1, [sp, #0x50]
        0xad038fe2, // stp q2, q3, [sp, #0x70]
        0xad0497e4, // stp q4, q5, [sp, #0x90]
        0xad059fe6, // stp q6, q7, [sp, #0xb0]
        0x3d80045f, // str q31, [x2, #0x10]
    };

    if (tcti_harness_run_generated_block(&cpu, 0x55a1c, init_file,
                                         sizeof(init_file) / sizeof(init_file[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    uint64_t wpos = 0;
    uint64_t copied_rpos = 0;
    uint64_t packed_flags = 0;
    int ret1 = a64_guest_read64(&cpu, cpu.tlb, stack_ptr + 0x38, &wpos);
    int ret2 = a64_guest_read64(&cpu, cpu.tlb, stack_ptr + 0x18, &copied_rpos);
    int ret3 = a64_guest_read64(&cpu, cpu.tlb, stack_ptr + 0x48, &packed_flags);

    mem_destroy(&mem);

    if (ret1 != A64_MEM_OK || ret2 != A64_MEM_OK || ret3 != A64_MEM_OK)
        return UINT64_MAX - 2;
    if (wpos != expected_wpos)
        return wpos;
    if (copied_rpos != expected_wpos)
        return 0x1000000000000000ULL | copied_rpos;
    if (packed_flags != 0xffffff80ffffffd0ULL)
        return 0x2000000000000000ULL | (packed_flags & 0x0fffffffffffffffULL);
    return 0;
}

uint64_t tcti_harness_case_musl_vdprintf_stack_file_zero_init(void)
{
    enum {
        stack_top = 0x130000,
        stack_ptr = stack_top - 0x120,
        file_ptr = stack_ptr + 0x38,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.sp = stack_top;
    cpu.x[0] = 2;

    memset(cpu.vregs[31].b, 0xa5, sizeof(cpu.vregs[31].b));

    static const uint32_t init_file[] = {
        0xa9ae7bfd, // stp x29, x30, [sp, #-0x120]!
        0x2a0003e4, // mov w4, w0
        0x4f00041f, // movi v31.4s, #0x0
        0x9100e3e0, // add x0, sp, #0x38
        0x910003fd, // mov x29, sp
        0xf900701f, // str xzr, [x0, #0xe0]
        0xad007c1f, // stp q31, q31, [x0]
        0xad017c1f, // stp q31, q31, [x0, #0x20]
        0xad027c1f, // stp q31, q31, [x0, #0x40]
        0xad037c1f, // stp q31, q31, [x0, #0x60]
        0xad047c1f, // stp q31, q31, [x0, #0x80]
        0xad057c1f, // stp q31, q31, [x0, #0xa0]
        0xad067c1f, // stp q31, q31, [x0, #0xc0]
        0x2f00041f, // mvni v31.2s, #0x0
        0xfc0c43ff, // stur d31, [sp, #0xc4]
    };

    if (tcti_harness_run_generated_block(&cpu, 0x57828, init_file,
                                         sizeof(init_file) / sizeof(init_file[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    uint64_t zero_slots[] = { 0, 0, 0, 0 };
    uint64_t negative_flags = 0;
    int ret = A64_MEM_OK;
    ret |= a64_guest_read64(&cpu, cpu.tlb, file_ptr + 0x20, &zero_slots[0]);
    ret |= a64_guest_read64(&cpu, cpu.tlb, file_ptr + 0x28, &zero_slots[1]);
    ret |= a64_guest_read64(&cpu, cpu.tlb, file_ptr + 0xc0, &zero_slots[2]);
    ret |= a64_guest_read64(&cpu, cpu.tlb, file_ptr + 0xe0, &zero_slots[3]);
    ret |= a64_guest_read64(&cpu, cpu.tlb, stack_ptr + 0xc4, &negative_flags);

    mem_destroy(&mem);

    if (ret != A64_MEM_OK)
        return UINT64_MAX - 2;
    for (size_t i = 0; i < sizeof(zero_slots) / sizeof(zero_slots[0]); i++) {
        if (zero_slots[i] != 0)
            return 0x1000000000000000ULL | (i << 48) | (zero_slots[i] & 0xffffffffffffULL);
    }
    if (negative_flags != UINT64_MAX)
        return 0x2000000000000000ULL | (negative_flags & 0x0fffffffffffffffULL);
    return 0;
}

uint64_t tcti_harness_case_dc_zva_zeroes_cache_block(void)
{
    enum {
        page_addr = 0x140000,
        zva_addr = page_addr + 0x80,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(page_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[3] = zva_addr + 17;

    for (uint64_t offset = 0; offset < 64; offset += 8) {
        if (a64_guest_write64(&cpu, cpu.tlb, zva_addr + offset, 0xababababababababULL) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }

    const uint32_t dc_zva_x3 = 0xd50b7423;
    if (tcti_harness_run_generated_block(&cpu, 0x184e4, &dc_zva_x3, 1) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t value = 0;
    for (uint64_t offset = 0; offset < 64; offset += 8) {
        if (a64_guest_read64(&cpu, cpu.tlb, zva_addr + offset, &value) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 3;
        }
        if (value != 0) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | offset | (value & 0x0000ffffffff0000ULL);
        }
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_harness_case_musl_strncmp_libc_reserved_prefix(void)
{
    enum {
        text_base = 0x5ee64,
        left_addr = 0x120000,
        right_addr = 0x121000,
    };

    static const uint32_t strncmp_insns[] = {
        0xb40001a2, // cbz x2, 0x5ee98
        0xd1000442, // sub x2, x2, #0x1
        0xd2800003, // mov x3, #0x0
        0x38636804, // ldrb w4, [x0, x3]
        0x38636825, // ldrb w5, [x1, x3]
        0x710000bf, // cmp w5, #0x0
        0x7a451080, // ccmp w4, w5, #0x0, ne
        0x34000084, // cbz w4, 0x5ee90
        0xfa430044, // ccmp x2, x3, #0x4, eq
        0x91000463, // add x3, x3, #0x1
        0x54ffff21, // b.ne 0x5ee70
        0x4b050080, // sub w0, w4, w5
        0xd65f03c0, // ret
        0x52800000, // mov w0, #0
        0x17fffffe, // b 0x5ee94
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(left_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(right_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_base;
    cpu.x[0] = left_addr;
    cpu.x[1] = right_addr;
    cpu.x[2] = 2;

    const char left[] = "c.musl-aarch64.so.1";
    const char right[] = "c.";
    for (size_t i = 0; i < sizeof(left); i++) {
        if (a64_guest_write8(&cpu, &tlb, left_addr + i, (uint8_t)left[i]) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }
    for (size_t i = 0; i < sizeof(right); i++) {
        if (a64_guest_write8(&cpu, &tlb, right_addr + i, (uint8_t)right[i]) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }

    for (unsigned step = 0; step < 64; step++) {
        if (cpu.pc == 0)
            break;
        if (cpu.pc < text_base || cpu.pc >= text_base + sizeof(strncmp_insns)) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | cpu.pc;
        }
        size_t index = (size_t)((cpu.pc - text_base) / 4);
        uint64_t old_pc = cpu.pc;
        if (tcti_harness_run_generated_block(&cpu, cpu.pc, &strncmp_insns[index], 1) < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | old_pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t result = (uint32_t)cpu.x[0];
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_musl_load_library_detects_libc_self(void)
{
    enum {
        text_base = 0x6b598,
        text_end = 0x6b630,
        fail_open_path = 0x6b544,
        self_detected_path = 0x6b5f0,
        self_path = 0x6b54c,
        strchr_pc = 0x5e920,
        strncmp_pc = 0x5ee64,
        libc_name_addr = 0x120000,
        reserved_libs_addr = 0x9db30,
    };

    static const uint32_t load_library_insns[] = {
        0x39400720, // ldrb w0, [x25, #0x1]
        0x7101a41f, // cmp w0, #0x69
        0x54fffd21, // b.ne 0x6b544
        0x39400b20, // ldrb w0, [x25, #0x2]
        0x7101881f, // cmp w0, #0x62
        0x54fffcc1, // b.ne 0x6b544
        0xd0000198, // adrp x24, 0x9d000
        0x912cc313, // add x19, x24, #0xb30
        0xaa1303f5, // mov x21, x19
        0xaa1303e0, // mov x0, x19
        0x528005c1, // mov w1, #0x2e
        0x97ffccd7, // bl 0x5e920
        0x91000413, // add x19, x0, #0x1
        0xaa0003f6, // mov x22, x0
        0xcb150262, // sub x2, x19, x21
        0xaa1503e1, // mov x1, x21
        0x91000f20, // add x0, x25, #0x3
        0x97ffce22, // bl 0x5ee64
        0x34000080, // cbz w0, 0x6b5f0
        0x394006c0, // ldrb w0, [x22, #0x1]
        0x35fffe80, // cbnz w0, 0x6b5b8
        0x17ffffd6, // b 0x6b544
        0x394002a0, // ldrb w0, [x21]
        0x34fffa80, // cbz w0, 0x6b544
        0xf00002a0, // adrp x0, 0xc2000
        0x91134000, // add x0, x0, #0x4d0
        0xf9400000, // ldr x0, [x0]
        0xf9003c00, // str x0, [x0, #0x78]
        0xf9403c00, // ldr x0, [x0, #0x78]
        0xf9003800, // str x0, [x0, #0x70]
        0xf9003400, // str x0, [x0, #0x68]
        0xf9003000, // str x0, [x0, #0x60]
        0xf9002c00, // str x0, [x0, #0x58]
        0xf9002800, // str x0, [x0, #0x50]
        0xf9002400, // str x0, [x0, #0x48]
        0xf9002000, // str x0, [x0, #0x40]
        0x52800024, // mov w4, #0x1
        0x17ffffc8, // b 0x6b54c
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(libc_name_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(reserved_libs_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_base;
    cpu.x[25] = libc_name_addr;

    const char libc_name[] = "libc.musl-aarch64.so.1";
    const char reserved_libs[] = "c.pthread.rt.m.dl.util.xnet";
    for (size_t i = 0; i < sizeof(libc_name); i++) {
        if (a64_guest_write8(&cpu, &tlb, libc_name_addr + i, (uint8_t)libc_name[i]) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }
    for (size_t i = 0; i < sizeof(reserved_libs); i++) {
        if (a64_guest_write8(&cpu, &tlb, reserved_libs_addr + i, (uint8_t)reserved_libs[i]) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
    }

    for (unsigned step = 0; step < 96; step++) {
        if (cpu.pc == self_detected_path) {
            mem_destroy(&mem);
            return 0;
        }
        if (cpu.pc == self_path) {
            uint64_t result = cpu.x[4] == 1 ? 0 : (0x3000000000000000ULL | cpu.x[4]);
            mem_destroy(&mem);
            return result;
        }
        if (cpu.pc == fail_open_path) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | cpu.pc;
        }
        if (cpu.pc == strchr_pc) {
            if (cpu.x[0] != reserved_libs_addr || (uint32_t)cpu.x[1] != '.') {
                uint64_t result = 0x4000000000000000ULL | (cpu.x[0] & 0x0000ffffffffffffULL);
                mem_destroy(&mem);
                return result;
            }
            cpu.x[0] = reserved_libs_addr + 1;
            cpu.pc = cpu.x[30];
            continue;
        }
        if (cpu.pc == strncmp_pc) {
            if (cpu.x[0] != libc_name_addr + 3 || cpu.x[1] != reserved_libs_addr ||
                cpu.x[2] != 2) {
                uint64_t result = 0x5000000000000000ULL | (cpu.x[0] & 0x0000ffffffffffffULL);
                mem_destroy(&mem);
                return result;
            }
            cpu.x[0] = 0;
            cpu.pc = cpu.x[30];
            continue;
        }
        if (cpu.pc < text_base || cpu.pc >= text_end || ((cpu.pc - text_base) & 3) != 0) {
            uint64_t result = 0x2000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }

        size_t index = (size_t)((cpu.pc - text_base) / 4);
        uint64_t old_pc = cpu.pc;
        if (tcti_harness_run_generated_block(&cpu, cpu.pc, &load_library_insns[index], 1) < 0) {
            mem_destroy(&mem);
            return 0x6000000000000000ULL | old_pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t result = 0x7000000000000000ULL | cpu.pc;
    mem_destroy(&mem);
    return result;
}

static uint32_t tcti_harness_musl_gnu_hash_expected(const char *s)
{
    uint32_t h = 5381;
    for (; *s; s++)
        h += h * 32 + (unsigned char)*s;
    return h;
}

static int tcti_harness_musl_gnu_hash_loop_insn(uint64_t pc, uint32_t *insn)
{
    switch (pc) {
    case 0x69f50:
        *insn = 0x14000004; // b 0x69f60
        return 0;
    case 0x69f54:
        *insn = 0x0b0d15ad; // add w13, w13, w13, lsl #5
        return 0;
    case 0x69f58:
        *insn = 0x91000463; // add x3, x3, #0x1
        return 0;
    case 0x69f5c:
        *insn = 0x0b0d002d; // add w13, w1, w13
        return 0;
    case 0x69f60:
        *insn = 0x39400061; // ldrb w1, [x3]
        return 0;
    case 0x69f64:
        *insn = 0x35ffff81; // cbnz w1, 0x69f54
        return 0;
    default:
        return -1;
    }
}

uint64_t tcti_harness_case_musl_gnu_hash_malloc(void)
{
    enum {
        text_base = 0x69f50,
        text_end = 0x69f68,
        name_addr = 0x120000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(name_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_base;
    cpu.x[3] = name_addr;
    cpu.x[13] = 5381;

    const char name[] = "malloc";
    for (size_t i = 0; i < sizeof(name); i++) {
        if (a64_guest_write8(&cpu, &tlb, name_addr + i, (uint8_t)name[i]) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }

    for (unsigned step = 0; step < 64; step++) {
        if (cpu.pc == text_end)
            break;
        uint32_t insn = 0;
        if (cpu.pc < text_base || cpu.pc >= text_end || ((cpu.pc - text_base) & 3) != 0 ||
            tcti_harness_musl_gnu_hash_loop_insn(cpu.pc, &insn) < 0) {
            uint64_t result = 0x1000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | old_pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint32_t actual = (uint32_t)cpu.x[13];
    uint32_t expected = tcti_harness_musl_gnu_hash_expected(name);
    mem_destroy(&mem);
    return actual == expected ? 0 : (0x3000000000000000ULL | actual);
}

uint64_t tcti_harness_case_musl_gnu_lookup_filtered_malloc(void)
{
    enum {
        text_base = 0x69884,
        text_end = 0x69968,
        hashtab_addr = 0x120000,
        dso_addr = 0x121000,
        symtab_addr = 0x122000,
        strings_addr = 0x123000,
        name_addr = 0x124000,
        sym_size = 24,
        sym_index = 1,
    };

    static const uint32_t lookup_insns[] = {
        0xb9400826, 0x2a0003ea, 0x510004c0, 0x0a040000, 0xd2800204, 0x8b204c80,
        0xf8606820, 0xea05001f, 0x540005e0, 0xb9400c24, 0x1ac42544, 0x9ac42404,
        0xd2800000, 0x36000564, 0xb9400025, 0xd37d7cc4, 0x91004084, 0x8b040024,
        0x1ac50946, 0x1b05a8c6, 0xb8667888, 0x34000468, 0xb9400420, 0x3200014a,
        0x5280030b, 0x4b000100, 0x8b254005, 0x8b050885, 0x14000004, 0x37000346,
        0x910010a5, 0x11000508, 0xb94000a6, 0x320000c0, 0x6b00015f, 0x54ffff41,
        0xf9402c41, 0x2a0803e0, 0xb4000061, 0x78e07821, 0x37fffea1, 0xf9402044,
        0x9bab7c01, 0xf9403049, 0x8b010080, 0xb8616881, 0x8b010129, 0xd2800001,
        0x38616864, 0x38616927, 0x6b07009f, 0x54fffd41, 0x91000421, 0x35ffff64,
        0x14000002, 0xd2800000, 0xd65f03c0,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(hashtab_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(dso_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(symtab_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(strings_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(name_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_base;

    const char name[] = "malloc";
    for (size_t i = 0; i < sizeof(name); i++) {
        if (a64_guest_write8(&cpu, &tlb, name_addr + i, (uint8_t)name[i]) != A64_MEM_OK ||
            a64_guest_write8(&cpu, &tlb, strings_addr + i, (uint8_t)name[i]) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }

    uint32_t hash = tcti_harness_musl_gnu_hash_expected(name);
    uint32_t bloom_shift = 6;
    uint32_t fofs = hash / 64;
    size_t fmask = (size_t)1 << (hash % 64);
    size_t bloom = fmask | ((size_t)1 << ((hash >> bloom_shift) % 64));

    if (a64_guest_write32(&cpu, &tlb, hashtab_addr + 0, 1) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 4, sym_index) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 8, 1) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 12, bloom_shift) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, hashtab_addr + 16, bloom) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 24, sym_index) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 28, hash | 1u) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x40, symtab_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x58, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x60, strings_addr) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, symtab_addr + sym_index * sym_size, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    cpu.x[0] = hash;
    cpu.x[1] = hashtab_addr;
    cpu.x[2] = dso_addr;
    cpu.x[3] = name_addr;
    cpu.x[4] = fofs;
    cpu.x[5] = fmask;
    cpu.x[30] = text_end;

    for (unsigned step = 0; step < 128; step++) {
        if (cpu.pc == text_end)
            break;
        if (cpu.pc < text_base || cpu.pc >= text_end || ((cpu.pc - text_base) & 3) != 0) {
            uint64_t result = 0x1000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }

        size_t index = (size_t)((cpu.pc - text_base) / 4);
        uint64_t old_pc = cpu.pc;
        int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &lookup_insns[index], 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | old_pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t expected = symtab_addr + sym_index * sym_size;
    uint64_t result = cpu.x[0] == expected ? 0 : (0x3000000000000000ULL | cpu.x[0]);
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_musl_gnu_lookup_dls2b_chain(void)
{
    enum {
        text_base = 0x69884,
        text_end = 0x69968,
        hashtab_addr = 0x120000,
        dso_addr = 0x123000,
        symtab_addr = 0x124000,
        strings_addr = 0x126000,
        name_addr = 0x127000,
        sym_size = 24,
        symoffset = 3,
        nbuckets = 1011,
        bloom_size = 256,
        bloom_shift = 14,
        bucket_index = 77,
        first_chain_sym = 118,
        target_sym = 121,
    };

    static const uint32_t lookup_insns[] = {
        0xb9400826, 0x2a0003ea, 0x510004c0, 0x0a040000, 0xd2800204, 0x8b204c80,
        0xf8606820, 0xea05001f, 0x540005e0, 0xb9400c24, 0x1ac42544, 0x9ac42404,
        0xd2800000, 0x36000564, 0xb9400025, 0xd37d7cc4, 0x91004084, 0x8b040024,
        0x1ac50946, 0x1b05a8c6, 0xb8667888, 0x34000468, 0xb9400420, 0x3200014a,
        0x5280030b, 0x4b000100, 0x8b254005, 0x8b050885, 0x14000004, 0x37000346,
        0x910010a5, 0x11000508, 0xb94000a6, 0x320000c0, 0x6b00015f, 0x54ffff41,
        0xf9402c41, 0x2a0803e0, 0xb4000061, 0x78e07821, 0x37fffea1, 0xf9402044,
        0x9bab7c01, 0xf9403049, 0x8b010080, 0xb8616881, 0x8b010129, 0xd2800001,
        0x38616864, 0x38616927, 0x6b07009f, 0x54fffd41, 0x91000421, 0x35ffff64,
        0x14000002, 0xd2800000, 0xd65f03c0,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(hashtab_addr), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(dso_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(symtab_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(strings_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(name_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_base;

    const char *names[] = { "fgetws_unlocked", "dn_comp", "towlower", "__dls2b" };
    uint32_t name_offsets[] = { 0x20, 0x40, 0x60, 0x80 };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *name = names[i];
        for (size_t j = 0; j <= strlen(name); j++) {
            if (a64_guest_write8(&cpu, &tlb, strings_addr + name_offsets[i] + j,
                                 (uint8_t)name[j]) != A64_MEM_OK) {
                mem_destroy(&mem);
                return UINT64_MAX - 1;
            }
        }
    }
    for (size_t j = 0; j <= strlen(names[3]); j++) {
        if (a64_guest_write8(&cpu, &tlb, name_addr + j, (uint8_t)names[3][j]) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
    }

    uint32_t hash = tcti_harness_musl_gnu_hash_expected(names[3]);
    uint32_t fofs = hash / 64;
    uint32_t bloom_index = fofs & (bloom_size - 1);
    size_t fmask = (size_t)1 << (hash % 64);
    size_t bloom = fmask | ((size_t)1 << ((hash >> bloom_shift) % 64));
    uint64_t bloom_addr = hashtab_addr + 16;
    uint64_t buckets_addr = bloom_addr + bloom_size * 8;
    uint64_t chains_addr = buckets_addr + nbuckets * 4;
    uint32_t chain_hashes[] = { 0x17d6d708, 0x1d061604, 0x077f36a8, hash | 1u };

    if (a64_guest_write32(&cpu, &tlb, hashtab_addr + 0, nbuckets) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 4, symoffset) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 8, bloom_size) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 12, bloom_shift) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, bloom_addr + bloom_index * 8, bloom) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, buckets_addr + bucket_index * 4, first_chain_sym) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x40, symtab_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x58, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x60, strings_addr) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 3;
    }

    for (uint32_t i = 0; i < 4; i++) {
        uint32_t sym = first_chain_sym + i;
        if (a64_guest_write32(&cpu, &tlb, chains_addr + (sym - symoffset) * 4,
                              chain_hashes[i]) != A64_MEM_OK ||
            a64_guest_write32(&cpu, &tlb, symtab_addr + sym * sym_size, name_offsets[i]) !=
                A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 4;
        }
    }

    cpu.x[0] = hash;
    cpu.x[1] = hashtab_addr;
    cpu.x[2] = dso_addr;
    cpu.x[3] = name_addr;
    cpu.x[4] = fofs;
    cpu.x[5] = fmask;
    cpu.x[30] = text_end;

    for (unsigned step = 0; step < 256; step++) {
        if (cpu.pc == text_end)
            break;
        if (cpu.pc < text_base || cpu.pc >= text_end || ((cpu.pc - text_base) & 3) != 0) {
            uint64_t result = 0x1000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }

        size_t index = (size_t)((cpu.pc - text_base) / 4);
        uint64_t old_pc = cpu.pc;
        int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &lookup_insns[index], 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | old_pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t expected = symtab_addr + target_sym * sym_size;
    uint64_t result = cpu.x[0] == expected ? 0 : (0x3000000000000000ULL | cpu.x[0]);
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_musl_find_sym_dls2b_from_ldso(void)
{
    enum {
        find_sym_base = 0x69f28,
        find_sym_end = 0x6a068,
        lookup_base = 0x69884,
        lookup_end = 0x69968,
        return_pc = 0x6a068,
        hashtab_addr = 0x120000,
        dso_addr = 0x123000,
        symtab_addr = 0x124000,
        strings_addr = 0x126000,
        name_addr = 0x127000,
        stack_addr = 0x128000,
        sym_size = 24,
        symoffset = 3,
        nbuckets = 1011,
        bloom_size = 256,
        bloom_shift = 14,
        bucket_index = 77,
        first_chain_sym = 118,
        target_sym = 121,
        target_value = 0x6c31c,
    };

    static const uint32_t find_sym_insns[] = {
        0xa9bc7bfd, 0xaa0003ef, 0xaa0103f2, 0x910003fd, 0xaa0103e3, 0x5282a0ad,
        0xa90153f3, 0x2a0203f4, 0xa9025bf5, 0xf9001bf7, 0x14000004, 0x0b0d15ad,
        0x91000463, 0x0b0d002d, 0x39400061, 0x35ffff81, 0xd2800033, 0x53067db5,
        0x9acd2273, 0x5280000e, 0x528080d6, 0x52800cf7, 0x14000024, 0x340001ce,
        0xaa0f03e2, 0x2a0e03e1, 0xaa1203e0, 0x97fffe1c, 0x14000027, 0x0b0e1021,
        0x91000400, 0x12040c2e, 0x4a4e602e, 0x39400001, 0x35ffff61, 0x12006dce,
        0x17fffff4, 0xaa1203e0, 0x17fffffb, 0xf9400402, 0x39401001, 0xb50001a2,
        0x12000c22, 0x7100185f, 0x540001a1, 0x53047c21, 0x1ac12ac1, 0x36000141,
        0xf9401bf7, 0xaa0f03e1, 0xa94153f3, 0xa9425bf5, 0xa8c47bfd, 0xd65f03c0,
        0x12000c22, 0x1ac22ae2, 0x3707fea2, 0xf94035ef, 0xb400028f, 0xf94029e1,
        0xb4fffb61, 0xaa1303e5, 0x2a1503e4, 0xaa1203e3, 0xaa0f03e2, 0x2a0d03e0,
        0x97fffe15, 0xb4fffec0, 0x79400c01, 0x35fffc41, 0x35fffe74, 0x39401001,
        0x12000c22, 0x7100185f, 0x54fffde0, 0xf9400403, 0xb4fffda3, 0x17ffffea,
        0xd2800000, 0x17ffffe1,
    };
    static const uint32_t lookup_insns[] = {
        0xb9400826, 0x2a0003ea, 0x510004c0, 0x0a040000, 0xd2800204, 0x8b204c80,
        0xf8606820, 0xea05001f, 0x540005e0, 0xb9400c24, 0x1ac42544, 0x9ac42404,
        0xd2800000, 0x36000564, 0xb9400025, 0xd37d7cc4, 0x91004084, 0x8b040024,
        0x1ac50946, 0x1b05a8c6, 0xb8667888, 0x34000468, 0xb9400420, 0x3200014a,
        0x5280030b, 0x4b000100, 0x8b254005, 0x8b050885, 0x14000004, 0x37000346,
        0x910010a5, 0x11000508, 0xb94000a6, 0x320000c0, 0x6b00015f, 0x54ffff41,
        0xf9402c41, 0x2a0803e0, 0xb4000061, 0x78e07821, 0x37fffea1, 0xf9402044,
        0x9bab7c01, 0xf9403049, 0x8b010080, 0xb8616881, 0x8b010129, 0xd2800001,
        0x38616864, 0x38616927, 0x6b07009f, 0x54fffd41, 0x91000421, 0x35ffff64,
        0x14000002, 0xd2800000, 0xd65f03c0,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(hashtab_addr), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(dso_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(symtab_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(strings_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(name_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(stack_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = find_sym_base;
    cpu.sp = stack_addr + 0x800;
    cpu.x[0] = dso_addr;
    cpu.x[1] = name_addr;
    cpu.x[2] = 0;
    cpu.x[30] = return_pc;

    const char *names[] = { "fgetws_unlocked", "dn_comp", "towlower", "__dls2b" };
    uint32_t name_offsets[] = { 0x20, 0x40, 0x60, 0x80 };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *name = names[i];
        for (size_t j = 0; j <= strlen(name); j++) {
            if (a64_guest_write8(&cpu, &tlb, strings_addr + name_offsets[i] + j,
                                 (uint8_t)name[j]) != A64_MEM_OK) {
                mem_destroy(&mem);
                return UINT64_MAX - 1;
            }
        }
    }
    for (size_t j = 0; j <= strlen(names[3]); j++) {
        if (a64_guest_write8(&cpu, &tlb, name_addr + j, (uint8_t)names[3][j]) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
    }

    uint32_t hash = tcti_harness_musl_gnu_hash_expected(names[3]);
    uint32_t fofs = hash / 64;
    uint32_t bloom_index = fofs & (bloom_size - 1);
    size_t fmask = (size_t)1 << (hash % 64);
    size_t bloom = fmask | ((size_t)1 << ((hash >> bloom_shift) % 64));
    uint64_t bloom_addr = hashtab_addr + 16;
    uint64_t buckets_addr = bloom_addr + bloom_size * 8;
    uint64_t chains_addr = buckets_addr + nbuckets * 4;
    uint32_t chain_hashes[] = { 0x17d6d708, 0x1d061604, 0x077f36a8, hash | 1u };

    if (a64_guest_write32(&cpu, &tlb, hashtab_addr + 0, nbuckets) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 4, symoffset) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 8, bloom_size) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, hashtab_addr + 12, bloom_shift) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, bloom_addr + bloom_index * 8, bloom) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, buckets_addr + bucket_index * 4, first_chain_sym) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x40, symtab_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x50, hashtab_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x58, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x60, strings_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 0x68, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 3;
    }

    for (uint32_t i = 0; i < 4; i++) {
        uint32_t sym = first_chain_sym + i;
        if (a64_guest_write32(&cpu, &tlb, chains_addr + (sym - symoffset) * 4,
                              chain_hashes[i]) != A64_MEM_OK ||
            a64_guest_write32(&cpu, &tlb, symtab_addr + sym * sym_size, name_offsets[i]) !=
                A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 4;
        }
    }
    if (a64_guest_write8(&cpu, &tlb, symtab_addr + target_sym * sym_size + 4, 0x12) !=
            A64_MEM_OK ||
        a64_guest_write16(&cpu, &tlb, symtab_addr + target_sym * sym_size + 6, 10) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, symtab_addr + target_sym * sym_size + 8,
                          target_value) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 5;
    }

    uint64_t expected = symtab_addr + target_sym * sym_size;
    for (unsigned step = 0; step < 512; step++) {
        if (cpu.pc == return_pc)
            break;

        const uint32_t *insns = NULL;
        uint64_t text_base = 0;
        uint64_t text_end = 0;
        if (cpu.pc >= find_sym_base && cpu.pc < find_sym_end) {
            text_base = find_sym_base;
            text_end = find_sym_end;
            insns = find_sym_insns;
        } else if (cpu.pc >= lookup_base && cpu.pc < lookup_end) {
            text_base = lookup_base;
            text_end = lookup_end;
            insns = lookup_insns;
        } else {
            uint64_t result = 0x1000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }
        if (((cpu.pc - text_base) & 3) != 0) {
            uint64_t result = 0x2000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }

        size_t index = (size_t)((cpu.pc - text_base) / 4);
        uint64_t old_pc = cpu.pc;
        int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &insns[index], 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x3000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | old_pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t result = cpu.x[0] == expected ? 0 : (0x4000000000000000ULL | cpu.x[0]);
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_musl_find_sym_accepts_global_func(void)
{
    enum {
        text_base = 0x69fc4,
        success_pc = 0x69fe8,
        fail_pc = 0x6a00c,
        sym_addr = 0x120000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(sym_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(0x130000), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_base;
    cpu.x[0] = sym_addr;
    cpu.x[15] = 0xabcdef00;
    cpu.x[22] = 0x406;
    cpu.x[23] = 0x67;
    cpu.sp = 0x130000;

    if (a64_guest_write8(&cpu, &tlb, sym_addr + 4, 0x12) != A64_MEM_OK ||
        a64_guest_write16(&cpu, &tlb, sym_addr + 6, 1) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, sym_addr + 8, 0x28888) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (unsigned step = 0; step < 32; step++) {
        if (cpu.pc == success_pc) {
            mem_destroy(&mem);
            return 0;
        }
        if (cpu.pc == fail_pc) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | cpu.pc;
        }
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x69fc4:
            insn = 0xf9400402; // ldr x2, [x0, #0x8]
            break;
        case 0x69fc8:
            insn = 0x39401001; // ldrb w1, [x0, #0x4]
            break;
        case 0x69fcc:
            insn = 0xb50001a2; // cbnz x2, 0x6a000
            break;
        case 0x69fd0:
            insn = 0x12000c22; // and w2, w1, #0xf
            break;
        case 0x69fd4:
            insn = 0x7100185f; // cmp w2, #0x6
            break;
        case 0x69fd8:
            insn = 0x540001a1; // b.ne 0x6a00c
            break;
        case 0x69fdc:
            insn = 0x53047c21; // lsr w1, w1, #4
            break;
        case 0x69fe0:
            insn = 0x1ac12ac1; // asr w1, w22, w1
            break;
        case 0x69fe4:
            insn = 0x36000141; // tbz w1, #0x0, 0x6a00c
            break;
        case 0x6a000:
            insn = 0x12000c22; // and w2, w1, #0xf
            break;
        case 0x6a004:
            insn = 0x1ac22ae2; // asr w2, w23, w2
            break;
        case 0x6a008:
            insn = 0x3707fea2; // tbnz w2, #0x0, 0x69fdc
            break;
        default: {
            uint64_t result = 0x2000000000000000ULL | cpu.pc;
            mem_destroy(&mem);
            return result;
        }
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x3000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | old_pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t result = 0x4000000000000000ULL | cpu.pc;
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_harness_case_add_shifted_hot_uses_scratch_carrier(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x698f0;
    cpu.x[4] = 0x120018;
    cpu.x[5] = 1;

    static const uint32_t insn = 0x8b050885; // add x5, x4, x5, lsl #2
    int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    uint64_t expected = 0x12001c;
    return cpu.x[5] == expected ? 0 : (0x2000000000000000ULL | cpu.x[5]);
}

uint64_t tcti_harness_case_add_extended_uxtw_uses_32bit_operand(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x698ec;
    cpu.x[0] = 0;
    cpu.x[5] = 1;

    static const uint32_t insn = 0x8b254005; // add x5, x0, w5, uxtw
    int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    if (cpu.x[5] != 1)
        return 0x2000000000000000ULL | cpu.x[5];

    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x69898;
    cpu.x[0] = 3;
    cpu.x[4] = 0x10;

    static const uint32_t shifted_insn = 0x8b204c80; // add x0, x4, w0, uxtw #3
    run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &shifted_insn, 1);
    if (run_ret < 0)
        return 0x3000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    return cpu.x[0] == 0x28 ? 0 : (0x4000000000000000ULL | cpu.x[0]);
}

uint64_t tcti_harness_case_logical_imm_memory_backed_source_uses_distinct_scratch(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6b280;
    cpu.x[26] = 0xdULL;

    static const uint32_t insn = 0x927df35a; // and x26, x26, #0xfffffffffffffff8
    int run_ret = tcti_harness_run_generated_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    return cpu.x[26];
}

uint64_t tcti_harness_case_musl_malloc_sizeclass_rbit_clz(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x29f34;
    cpu.x[1] = 0x32ULL;

    static const uint32_t insns[] = {
        0x91000421, // add x1, x1, #0x1
        0x53027c20, // lsr w0, w1, #2
        0x2a410400, // orr w0, w0, w1, lsr #1
        0x2a400800, // orr w0, w0, w0, lsr #2
        0x2a401000, // orr w0, w0, w0, lsr #4
        0x2a402002, // orr w2, w0, w0, lsr #8
        0x11000442, // add w2, w2, #0x1
        0x5ac00042, // rbit w2, w2
        0x5ac01042, // clz w2, w2
        0x51000442, // sub w2, w2, #0x1
        0x531e7442, // lsl w2, w2, #2
        0x11000443, // add w3, w2, #0x1
    };

    int run_ret =
        tcti_harness_run_generated_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    return ((uint64_t)(uint32_t)cpu.x[2] << 32) | (uint32_t)cpu.x[3];
}
