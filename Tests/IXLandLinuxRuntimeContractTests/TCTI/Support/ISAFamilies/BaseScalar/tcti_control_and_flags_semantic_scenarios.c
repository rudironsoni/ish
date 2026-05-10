#include "tcti_control_and_flags_semantic_scenarios.h"

#include "../../Internal/tcti_semantic_runtime_support.h"

void tcti_semantic_single_gadget_snapshot(void (*gadget)(void), const uint64_t *in_regs,
                                         uint64_t *out_regs);

__asm__(".text\n"
        ".align 2\n"
        ".global _tcti_semantic_single_gadget_snapshot\n"
        "_tcti_semantic_single_gadget_snapshot:\n"
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

void tcti_semantic_seed_inputs(uint64_t regs[TCTI_SEMANTIC_INPUT_REGS])
{
    for (int i = 0; i < TCTI_SEMANTIC_INPUT_REGS; i++)
        regs[i] = 0xA500000000000000ULL + (uint64_t)i;
}

void tcti_semantic_run_snapshot(void (*gadget)(void),
                               const uint64_t in_regs[TCTI_SEMANTIC_INPUT_REGS],
                               uint64_t out_regs[TCTI_SEMANTIC_SNAPSHOT_REGS])
{
    memset(out_regs, 0, sizeof(uint64_t) * TCTI_SEMANTIC_SNAPSHOT_REGS);
    tcti_semantic_single_gadget_snapshot(gadget, in_regs, out_regs);
}


static uint64_t tcti_semantic_case_musl_memset_dup_fill(uint8_t fill_byte)
{
    enum {
        buffer_addr = 0x260000,
        buffer_words = 4,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(buffer_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = buffer_addr;
    cpu.x[1] = fill_byte;
    cpu.vregs[0].d[0] = 0xfeedfacefeedfaceULL;
    cpu.vregs[0].d[1] = 0xfeedfacefeedfaceULL;

    for (uint64_t offset = 0; offset < buffer_words * sizeof(uint64_t);
         offset += sizeof(uint64_t)) {
        if (a64_guest_write64(&cpu, cpu.tlb, buffer_addr + offset, 0xfeedfacefeedfaceULL) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }

    static const uint32_t insns[] = {
        0x4e010c20, // dup v0.16b, w1
        0x3d800000, // str q0, [x0]
        0x3d800400, // str q0, [x0, #0x10]
        0xad010000, // stp q0, q0, [x0, #0x20]
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, 0x28420, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = 0;
    uint64_t expected = fill_byte;
    expected |= expected << 8;
    expected |= expected << 16;
    expected |= expected << 32;
    for (uint64_t word = 0; word < buffer_words; word++) {
        uint64_t value = 0;
        if (a64_guest_read64(&cpu, cpu.tlb, buffer_addr + word * sizeof(uint64_t), &value) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
        if (value != expected) {
            result = (word << 56) | (value & 0x00ffffffffffffffULL);
            break;
        }
    }

    mem_destroy(&mem);
    return result;
}

int tcti_semantic_case_add_7_13_14(tcti_semantic_snapshot_t *snapshot)
{
    uint64_t lhs = 0xFFFFFEF7FBF0ULL;
    uint64_t rhs = 0x8ULL;

    tcti_semantic_seed_inputs(snapshot->in_regs);
    snapshot->in_regs[0] = 0x1111111111111111ULL;
    snapshot->in_regs[13] = lhs; // guest x13 -> host x14
    snapshot->in_regs[14] = rhs; // guest x14 -> host x15

    tcti_semantic_run_snapshot(gadget_add_reg[7][13][14], snapshot->in_regs, snapshot->out_regs);

    // guest x7 (dest) -> host x8 (out_regs[7]), guest x13 -> host x14 (out_regs[13]), guest x14 ->
    // host x15 (out_regs[14])
    return snapshot->out_regs[7] == (lhs + rhs) && snapshot->out_regs[13] == lhs &&
           snapshot->out_regs[14] == rhs && snapshot->out_regs[0] == 0x1111111111111111ULL;
}

uint64_t tcti_semantic_case_busybox_prompt_first_turn_ldurb_csel_hi_block(void)
{
    enum {
        data_base = 0x2000,
        result_addr = 0x2100,
        text_pc = 0x79600,
        return_pc = 0x9000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(data_base), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_pc;
    cpu.x[0] = data_base;
    cpu.x[1] = 0;
    cpu.x[2] = 0;
    cpu.x[3] = result_addr;
    cpu.x[5] = 0;
    cpu.x[6] = data_base + 3;
    cpu.x[30] = return_pc;
    cpu.pstate = 0;

    if (a64_guest_write8(&cpu, &tlb, data_base + 0, 0) != A64_MEM_OK ||
        a64_guest_write8(&cpu, &tlb, data_base + 1, 0) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, result_addr, 0xdeadbeefU) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0x39400408, // ldrb w8, [x0, #1]
        0x91000800, // add x0, x0, #2
        0x385fe004, // ldurb w4, [x0, #-2]
        0x531a6508, // lsl w8, w8, #6
        0x0b440904, // add w4, w8, w4, lsr #2
        0x53155028, // lsl w8, w1, #11
        0x92720108, // and x8, x8, #0x4000
        0x8b24c104, // add x4, x8, w4, sxtw
        0xcb040044, // sub x4, x2, x4
        0xeb04005f, // cmp x2, x4
        0x54000181, // b.ne 0x79658
        0xeb0000df, // cmp x6, x0
        0x12800061, // mov w1, #-4
        0x128000e0, // mov w0, #-8
        0x1a818000, // csel w0, w0, w1, hi
        0x1a9f1000, // csel w0, w0, wzr, ne
        0xb9000062, // str w2, [x3]
        0xd65f03c0, // ret
    };

    int run_ret =
        a64_cpu_execute_code_program(&cpu, text_pc, insns, sizeof(insns) / sizeof(insns[0]), 8);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint32_t stored = 0xffffffffU;
    if (a64_guest_read32(&cpu, &tlb, result_addr, &stored) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t failure = 0;
    if (cpu.pc != return_pc)
        failure = 0x2000000000000000ULL | cpu.pc;
    else if (cpu.x[0] != 0x00000000fffffff8ULL)
        failure = 0x3000000000000000ULL | cpu.x[0];
    else if (cpu.x[4] != 0)
        failure = 0x4000000000000000ULL | cpu.x[4];
    else if (stored != 0)
        failure = 0x5000000000000000ULL | stored;

    mem_destroy(&mem);
    return failure;
}

uint64_t tcti_semantic_case_busybox_prompt_loop_ccmp_bls_exits_taken_path(void)
{
    enum {
        data_base = 0x3000,
        result_addr = 0x3100,
        text_pc = 0x79664,
        exit_pc = 0x79714,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(data_base), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = text_pc;
    cpu.x[0] = data_base;
    cpu.x[1] = 0;
    cpu.x[2] = 0x1000;
    cpu.x[5] = 0x1010;
    cpu.pstate = 0;

    if (a64_guest_write8(&cpu, &tlb, data_base, 0x04) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, result_addr, 0xdeadbeefU) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xaa2103e4, // mvn x4, x1
        0x39400001, // ldrb w1, [x0]
        0xcb210881, // sub x1, x4, w1, uxtb #2
        0x8b010044, // add x4, x2, x1
        0xeb0400bf, // cmp x5, x4
        0xfa449040, // ccmp x2, x4, #0x0, ls
        0x540004c9, // b.ls 0x79714
    };

    int run_ret =
        a64_cpu_execute_code_program(&cpu, text_pc, insns, sizeof(insns) / sizeof(insns[0]), 4);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t failure = 0;
    if (cpu.pc != exit_pc)
        failure = 0x2000000000000000ULL | cpu.pc;
    else if (cpu.x[1] != UINT64_C(0xffffffffffffffef))
        failure = 0x3000000000000000ULL | cpu.x[1];
    else if (cpu.x[4] != UINT64_C(0x0000000000000fef))
        failure = 0x4000000000000000ULL | cpu.x[4];

    mem_destroy(&mem);
    return failure;
}

uint64_t tcti_semantic_case_cmp_add_csel_ne_uses_preserved_zero_flag(void)
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
        0xd65f03c0, // ret
    };

    if (a64_cpu_execute_code_block(&cpu, 0x51f1c, insns, sizeof(insns) / sizeof(insns[0])) <
        0) {
        return UINT64_MAX;
    }

    return cpu.x[2];
}

uint64_t tcti_semantic_case_cmp_ccmp_false_immediate_clears_zero(void)
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

    if (a64_cpu_execute_code_block(&cpu, 0x51f38, insns, sizeof(insns) / sizeof(insns[0])) <
        0) {
        return UINT64_MAX;
    }

    return cpu.pc;
}

uint64_t tcti_semantic_case_cmp_csel_ls_hs_tracks_unsigned_minmax(void)
{
    void *gadgets[] = {
        (void *)gadget_addsub_reg_fallback,
        (void *)31, // rd: CMP alias writes XZR
        (void *)4,  // rn
        (void *)2,  // rm
        (void *)0,  // shift_type
        (void *)0,  // imm_shift
        (void *)1,  // SUB
        (void *)1,  // set_flags
        (void *)1,  // is_64bit
        (void *)gadget_csel_fallback,
        (void *)4,      // rd
        (void *)4,      // rn
        (void *)2,      // rm
        (void *)A64_LS, // select unsigned min(x4, x2)
        (void *)0,
        (void *)1,
        (void *)gadget_addsub_reg_fallback,
        (void *)31, // rd: CMP alias writes XZR
        (void *)5,  // rn
        (void *)2,  // rm
        (void *)0,
        (void *)0,
        (void *)1,
        (void *)1,
        (void *)1,
        (void *)gadget_csel_fallback,
        (void *)5,      // rd
        (void *)5,      // rn
        (void *)2,      // rm
        (void *)A64_CS, // select unsigned max(x5, x2)
        (void *)0,
        (void *)1,
        (void *)tcti_exit_block,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[2] = 0x4000ULL;
    cpu.x[4] = 0x9000ULL;
    cpu.x[5] = 0x1000ULL;

    tcti_entry_block(gadgets, &cpu);

    if (cpu.x[4] != 0x4000ULL)
        return cpu.x[4];
    if (cpu.x[5] != 0x4000ULL)
        return cpu.x[5];
    return 0ULL;
}

uint64_t tcti_semantic_case_cmp_csinv_ls_preserves_nonoverflow_size(void)
{
    enum {
        A64_CSEL_CSINV = 2,
    };

    void *gadgets[] = {
        (void *)gadget_addsub_reg_fallback,
        (void *)31, // rd: CMP alias writes XZR
        (void *)3,  // rn
        (void *)2,  // rm
        (void *)0,
        (void *)0,
        (void *)1,
        (void *)1,
        (void *)1,
        (void *)gadget_csel_fallback,
        (void *)1,      // rd
        (void *)1,      // rn
        (void *)31,     // rm: XZR, so false arm becomes ~0
        (void *)A64_LS, // keep x1 on non-overflow, saturate on overflow
        (void *)A64_CSEL_CSINV,
        (void *)1,
        (void *)tcti_exit_block,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x1234ULL;
    cpu.x[2] = 0x20ULL;
    cpu.x[3] = 0x10ULL;

    tcti_entry_block(gadgets, &cpu);
    return cpu.x[1];
}

uint64_t tcti_semantic_case_cmp_csinv_ls_saturates_overflow_size(void)
{
    enum {
        A64_CSEL_CSINV = 2,
    };

    void *gadgets[] = {
        (void *)gadget_addsub_reg_fallback,
        (void *)31, // rd: CMP alias writes XZR
        (void *)3,  // rn
        (void *)2,  // rm
        (void *)0,
        (void *)0,
        (void *)1,
        (void *)1,
        (void *)1,
        (void *)gadget_csel_fallback,
        (void *)1,
        (void *)1,
        (void *)31,
        (void *)A64_LS,
        (void *)A64_CSEL_CSINV,
        (void *)1,
        (void *)tcti_exit_block,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x1234ULL;
    cpu.x[2] = 0x10ULL;
    cpu.x[3] = 0x20ULL;

    tcti_entry_block(gadgets, &cpu);
    return cpu.x[1];
}

uint64_t tcti_semantic_case_cmp_w20_ccmp_gt_bls_uses_32bit_flags(void)
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
        (void *)25,              // rn
        (void *)21,              // rm
        (void *)0,               // imm_operand
        (void *)A64_GT,          // cond
        (void *)0,               // nzcv false-condition immediate
        (void *)A64_DP_REG_CCMP, // subtype
        (void *)1,               // is_64bit
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

uint64_t tcti_semantic_case_cmp_w2_w1_uxtb_csel_uses_w_width(void)
{
    enum {
        branch_target = 0x1000,
    };

    void *gadgets[] = {
        (void *)gadget_addsub_ext_fallback,
        (void *)31, // rd: WZR
        (void *)2,  // rn: W2
        (void *)1,  // rm: W1
        (void *)A64_EXT_UXTB,
        (void *)0,                           // imm_shift
        (void *)((1ULL << 0) | (1ULL << 1)), // SUBS, 32-bit
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
    cpu.x[0] = 0x123456789abcdef0ULL;
    cpu.x[1] = 0x2e;
    cpu.x[2] = 0xffffffff0000002eULL;

    tcti_entry_block(gadgets, &cpu);

    return cpu.x[0];
}

uint64_t tcti_semantic_case_csel_eq_selects_true_operand(void)
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

uint64_t tcti_semantic_case_csel_preserves_flags_for_bcond(void)
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

uint64_t tcti_semantic_case_entry_restores_pstate_for_bcond_ne(void)
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
                     : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                       "x12", "x13", "x14", "x15", "x16", "x17", "x30", "cc", "memory");

    return cpu.pc;
}

uint64_t tcti_semantic_case_generated_cinc_ne_increments_only_on_ne(void)
{
    static const uint32_t equal_insns[] = {
        0xf1100c5f, // cmp x2, #0x403
        0x9a800400, // cinc x0, x0, ne
    };
    static const uint32_t notequal_insns[] = {
        0xf110105f, // cmp x2, #0x404
        0x9a800400, // cinc x0, x0, ne
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 7;
    cpu.x[2] = 0x403;

    if (a64_cpu_execute_code_block(&cpu, 0x6c284, equal_insns,
                                         sizeof(equal_insns) / sizeof(equal_insns[0])) < 0) {
        return UINT64_MAX;
    }
    if (cpu.x[0] != 7)
        return cpu.x[0];

    cpu.pc = 0x6c284;
    cpu.x[0] = 7;
    cpu.x[2] = 0x405;
    if (a64_cpu_execute_code_block(&cpu, 0x6c284, notequal_insns,
                                         sizeof(notequal_insns) / sizeof(notequal_insns[0])) < 0) {
        return UINT64_MAX - 1;
    }
    return cpu.x[0];
}

uint64_t tcti_semantic_case_generated_cinv_w_ne_zero_extends(void)
{
    static const uint32_t insns[] = {
        0x5a880108, // cinv w8, w8, ne
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0; // Z clear, so NE holds for the alias.
    cpu.x[8] = 0xffffffff0000000fULL;

    if (a64_cpu_execute_code_block(&cpu, 0x6bfb8, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0) {
        return UINT64_MAX;
    }
    return cpu.x[8];
}

uint64_t tcti_semantic_case_generated_cneg_w_ne_zero_extends(void)
{
    static const uint32_t insns[] = {
        0x5a890529, // cneg w9, w9, ne
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0; // Z clear, so NE holds for the alias.
    cpu.x[9] = 0xfffffffffffffffeULL;

    if (a64_cpu_execute_code_block(&cpu, 0x6bfbc, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0) {
        return UINT64_MAX;
    }
    return cpu.x[9];
}

uint64_t tcti_semantic_case_generated_csetm_w_ne_zero_extends(void)
{
    static const uint32_t insns[] = {
        0x5a9f03e6, // csetm w6, ne
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0; // Z clear, so NE holds for the alias.
    cpu.x[6] = 0xaaaaaaaaaaaaaaaaULL;

    if (a64_cpu_execute_code_block(&cpu, 0x6bfb4, insns,
                                         sizeof(insns) / sizeof(insns[0])) < 0) {
        return UINT64_MAX;
    }
    return cpu.x[6];
}

uint64_t tcti_semantic_case_generated_vsnprintf_zero_size_length(void)
{
    static const uint32_t block[] = {
        0xf100003f, // cmp x1, #0
        0xaa0203e5, // mov x5, x2
        0x910103e2, // add x2, sp, #0x40
        0x910003fd, // mov x29, sp
        0x9a800042, // csel x2, x2, x0, eq
        0x9a9f07e0, // cset x0, ne
        0x4f00041f, // movi v31.4s, #0
        0xcb000020, // sub x0, x1, x0
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0;
    cpu.x[1] = 0;
    cpu.x[2] = 0x1234567812345678ULL;
    cpu.sp = 0xfffffef7f810ULL;

    if (a64_cpu_execute_code_block(&cpu, 0x6c7b0, block, sizeof(block) / sizeof(block[0])) < 0)
        return UINT64_MAX - 16;

    return cpu.x[0];
}

int tcti_semantic_case_mov_2_7(tcti_semantic_snapshot_t *snapshot)
{
    tcti_semantic_seed_inputs(snapshot->in_regs);
    snapshot->in_regs[7] = 0x123456789ABCDEF0ULL; // guest x7 (source) -> host x8
    snapshot->in_regs[0] = 0x4444444444444444ULL; // guest x0 -> host x1
    snapshot->in_regs[2] = 0;                     // guest x2 (dest) -> host x3

    tcti_semantic_run_snapshot(gadget_mov_reg[2][7], snapshot->in_regs, snapshot->out_regs);

    // MOV x2, x7: guest x2 (host x3) = guest x7 (host x8)
    return snapshot->out_regs[2] == 0x123456789ABCDEF0ULL &&
           snapshot->out_regs[7] == 0x123456789ABCDEF0ULL &&
           snapshot->out_regs[0] == 0x4444444444444444ULL;
}

uint64_t tcti_semantic_case_musl_memset_dup_replicates_byte_fill(void)
{
    return tcti_semantic_case_musl_memset_dup_fill(0x21);
}

uint64_t tcti_semantic_case_musl_memset_dup_zeroes_vector_store(void)
{
    return tcti_semantic_case_musl_memset_dup_fill(0);
}

uint64_t tcti_semantic_case_strchrnul_byte_loop_stops_on_match_or_nul(void)
{
    enum {
        text_base = 0x6e9dc,
        mov_pc = 0x6e9f0,
        ret_pc = 0x6e9f4,
        data_base = 0x220000,
        data_page = data_base & ~(PAGE_SIZE - 1),
        max_block_entries = 16,
    };

    static const uint32_t loop_insns[] = {
        0x91000442, // add x2, x2, #1
        0x39400040, // ldrb w0, [x2]
        0x7100001f, // cmp w0, #0
        0x7a411004, // ccmp w0, w1, #4, ne
        0x54ffff81, // b.ne 0x6e9dc
    };
    static const uint32_t mov_insn = 0xaa0203e0; // mov x0, x2
    static const uint32_t ret_insn = 0xd65f03c0; // ret

    uint64_t result = 0;
    const struct {
        const char bytes[4];
        uint32_t needle;
        uint64_t expected_addr;
        uint64_t base_tag;
    } cases[] = {
        { { 'D', '\0', '\0', '\0' }, 'D', data_base, 0x1000000000000000ULL },
        { { 'Z', '\0', '.', '\0' }, '.', data_base + 1, 0x2000000000000000ULL },
    };

    for (size_t case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
        struct mem mem;
        mem_init(&mem);
        if (pt_map_nothing(&mem, PAGE(data_page), 1, P_READ | P_WRITE) < 0) {
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
        cpu.x[1] = cases[case_index].needle;
        cpu.x[2] = data_base - 1;
        cpu.x[30] = 0;
        cpu.pstate = 0;

        for (size_t i = 0; i < sizeof(cases[case_index].bytes); i++) {
            if (a64_guest_write8(&cpu, &tlb, data_base + i, (uint8_t)cases[case_index].bytes[i]) !=
                A64_MEM_OK) {
                mem_destroy(&mem);
                return UINT64_MAX - 1;
            }
        }

        bool finished = false;
        for (size_t entry = 0; entry < max_block_entries; entry++) {
            cpu.pc = text_base;
            if (a64_cpu_execute_code_block(&cpu, text_base, loop_insns,
                                                 sizeof(loop_insns) / sizeof(loop_insns[0])) < 0) {
                mem_destroy(&mem);
                return UINT64_MAX - 2;
            }
            if (cpu.pc == text_base)
                continue;
            if (cpu.pc != mov_pc)
                break;

            if (a64_cpu_execute_code_block(&cpu, mov_pc, &mov_insn, 1) < 0) {
                mem_destroy(&mem);
                return UINT64_MAX - 3;
            }
            if (cpu.pc == mov_pc)
                cpu.pc = ret_pc;
            if (cpu.pc != ret_pc)
                break;

            if (a64_cpu_execute_code_block(&cpu, ret_pc, &ret_insn, 1) < 0) {
                mem_destroy(&mem);
                return UINT64_MAX - 4;
            }
            if (cpu.pc == 0) {
                finished = true;
                break;
            }
            break;
        }

        if (!finished)
            result |= cases[case_index].base_tag | 0x00ff000000000000ULL;
        if (cpu.x[0] != cases[case_index].expected_addr)
            result |= cases[case_index].base_tag | (cpu.x[0] & 0x0000ffffffffffffULL);
        if (cpu.x[2] != cases[case_index].expected_addr)
            result |= (cases[case_index].base_tag >> 4) | (cpu.x[2] & 0x0000ffffffffffffULL);

        mem_destroy(&mem);
    }

    return result;
}

uint64_t tcti_semantic_case_strchrnul_vector_mask_finds_dot(void)
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
        (void *)4, // rd
        (void *)3, // rn
        (void *)6, // rm
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)6, // subtype: EON
        (void *)0,
        (void *)1,
        (void *)gadget_add_reg[5][5][7],
        (void *)gadget_add_reg[0][3][7],
        (void *)gadget_logical_reg_fallback,
        (void *)4, // rd
        (void *)4, // rn
        (void *)5, // rm
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)0, // subtype: AND
        (void *)0,
        (void *)1,
        (void *)gadget_logical_reg_fallback,
        (void *)0, // rd
        (void *)0, // rn
        (void *)3, // rm
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)4, // subtype: BIC
        (void *)0,
        (void *)1,
        (void *)gadget_logical_reg_fallback,
        (void *)0, // rd
        (void *)0, // rn
        (void *)4, // rm
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

uint64_t tcti_semantic_case_tpidr_el0_roundtrips_through_full_sysreg_encoding(void)
{
    enum {
        text_base = 0x618e0,
    };

    static const uint32_t insns[] = {
        0xd51bd040, // msr tpidr_el0, x0  (sysreg bits[19:5] = 0xde82)
        0xd53bd041, // mrs x1, tpidr_el0
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = text_base;
    cpu.x[0] = 0x123456789abcdef0ULL;

    for (size_t i = 0; i < sizeof(insns) / sizeof(insns[0]); i++) {
        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insns[i], 1);
        if (run_ret < 0)
            return 0x1000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | old_pc;
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t result = 0;
    result |= (cpu.tpidr_el0 == 0x123456789abcdef0ULL) ? 0 : 1;
    result |= (cpu.x[1] == 0x123456789abcdef0ULL) ? 0 : 2;
    return result;
}

uint64_t tcti_semantic_case_vsnprintf_zero_size_cset_ne_preserves_zero_flag(void)
{
    enum {
        A64_CSEL_CSEL = 0,
        A64_CSEL_CSINC = 1,
    };

    void *gadgets[] = {
        (void *)gadget_addsub_imm_fallback,
        (void *)31, // rd: CMP alias writes XZR
        (void *)1,  // rn
        (void *)0,
        (void *)1, // SUB
        (void *)1, // set_flags
        (void *)1, // is_64bit
        (void *)0, // rd_is_sp
        (void *)0, // rn_is_sp
        (void *)gadget_csel_fallback,
        (void *)2,      // rd
        (void *)2,      // rn
        (void *)0,      // rm
        (void *)A64_EQ, // csel x2,x2,x0,eq
        (void *)A64_CSEL_CSEL,
        (void *)1,
        (void *)gadget_csel_fallback,
        (void *)0,      // rd
        (void *)31,     // rn: xzr
        (void *)31,     // rm: xzr
        (void *)A64_EQ, // cset ne is csinc x0,xzr,xzr,eq
        (void *)A64_CSEL_CSINC,
        (void *)1,
        (void *)gadget_addsub_reg_fallback,
        (void *)0, // rd
        (void *)1, // rn
        (void *)0, // rm
        (void *)0, // shift_type
        (void *)0, // imm_shift
        (void *)1, // SUB
        (void *)0, // set_flags
        (void *)1, // is_64bit
        (void *)tcti_exit_block,
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0;
    cpu.x[2] = 0x1234567812345678ULL;

    tcti_entry_block(gadgets, &cpu);

    return cpu.x[0];
}
