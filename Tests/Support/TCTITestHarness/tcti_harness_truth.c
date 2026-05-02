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
            return -2;
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
