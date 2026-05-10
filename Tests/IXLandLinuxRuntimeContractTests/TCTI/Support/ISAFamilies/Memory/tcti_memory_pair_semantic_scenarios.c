#include "tcti_memory_pair_semantic_scenarios.h"

#include "../../Internal/tcti_semantic_runtime_support.h"

uint64_t tcti_semantic_case_add_hot_pair_to_memory_backed_x23(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x5655ab48ULL;
    cpu.x[2] = 0xc8ULL;
    cpu.x[23] = 0x1111111111111111ULL;

    static const uint32_t insns[] = {
        0x8b020037, // add x23, x1, x2
    };

    if (a64_cpu_execute_code_block(&cpu, 0x6a0c8, insns, sizeof(insns) / sizeof(insns[0])) <
        0)
        return UINT64_MAX;

    return cpu.x[23];
}

uint64_t tcti_semantic_case_busybox_stack_canary_equal_path_branches_to_restore(void)
{
    enum {
        stack_ptr = 0x132000,
        stack_slot = stack_ptr + 0x8,
        canary_page = 0x140000,
        canary_ptr_slot = canary_page + 0xee8,
        canary_value_addr = 0x141000,
        start_pc = 0x79790,
        success_pc = 0x79860,
    };
    const uint64_t canary_value = 0x0123456789abcdefULL;

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(canary_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = start_pc;
    cpu.sp = stack_ptr;
    cpu.x[20] = canary_page;

    if (a64_guest_write64(&cpu, &tlb, stack_slot, canary_value) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, canary_ptr_slot, canary_value_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, canary_value_addr, canary_value) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xf9477694, // ldr x20, [x20, #0xee8]
        0xf94007e0, // ldr x0, [sp, #0x8]
        0xf9400281, // ldr x1, [x20]
        0xeb010000, // subs x0, x0, x1
        0xd2800001, // mov x1, #0
        0x540005e0, // b.eq 0x79860
    };

    int run_ret = a64_cpu_execute_code_program(&cpu, start_pc, insns,
                                                 sizeof(insns) / sizeof(insns[0]), 6);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = 0;
    if (cpu.pc != success_pc)
        result |= 1ULL;
    if (cpu.x[20] != canary_value_addr)
        result |= 2ULL;
    if (cpu.sp != stack_ptr)
        result |= 4ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_busybox_stack_canary_mismatch_calls_fail_path(void)
{
    enum {
        stack_ptr = 0x133000,
        stack_slot = stack_ptr + 0x8,
        canary_page = 0x142000,
        canary_ptr_slot = canary_page + 0xee8,
        canary_value_addr = 0x143000,
        start_pc = 0x79790,
        fail_pc = 0x797a8,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(canary_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = start_pc;
    cpu.sp = stack_ptr;
    cpu.x[20] = canary_page;

    if (a64_guest_write64(&cpu, &tlb, stack_slot, 0x1111111111111111ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, canary_ptr_slot, canary_value_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, canary_value_addr, 0x2222222222222222ULL) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xf9477694, // ldr x20, [x20, #0xee8]
        0xf94007e0, // ldr x0, [sp, #0x8]
        0xf9400281, // ldr x1, [x20]
        0xeb010000, // subs x0, x0, x1
        0xd2800001, // mov x1, #0
        0x540005e0, // b.eq 0x79860
    };

    int run_ret = a64_cpu_execute_code_program(&cpu, start_pc, insns,
                                                 sizeof(insns) / sizeof(insns[0]), 6);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = 0;
    if (cpu.pc != fail_pc)
        result |= 1ULL;
    if (cpu.x[20] != canary_value_addr)
        result |= 2ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_busybox_stack_restore_block_restores_frame(void)
{
    enum {
        stack_ptr = 0x132000,
        saved_x30_slot = stack_ptr + 0x10,
        saved_x19_slot = stack_ptr + 0x18,
        saved_x20_slot = stack_ptr + 0x20,
        saved_x21_slot = stack_ptr + 0x28,
        saved_x22_slot = stack_ptr + 0x30,
        saved_x23_slot = stack_ptr + 0x38,
        start_pc = 0x79860,
        return_pc = 0x9000,
    };
    const uint64_t saved_x19 = 0x1111222233334444ULL;
    const uint64_t saved_x20 = 0x2222333344445555ULL;
    const uint64_t saved_x21 = 0x5555666677778888ULL;
    const uint64_t saved_x22 = 0x9999aaaabbbbccccULL;
    const uint64_t saved_x23 = 0xddddeeeeffff0001ULL;
    const uint64_t saved_x30 = return_pc;

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
    cpu.pc = start_pc;
    cpu.sp = stack_ptr;
    cpu.x[19] = 0xaaaaaaaaaaaaaaaaULL;
    cpu.x[20] = 0xbbbbbbbbbbbbbbbbULL;
    cpu.x[21] = 0xccccccccccccccccULL;
    cpu.x[22] = 0xddddddddddddddddULL;
    cpu.x[23] = 0xeeeeeeeeeeeeeeeeULL;
    cpu.x[30] = 0xffffffffffffffffULL;

    if (a64_guest_write64(&cpu, &tlb, saved_x30_slot, saved_x30) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, saved_x19_slot, saved_x19) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, saved_x20_slot, saved_x20) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, saved_x21_slot, saved_x21) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, saved_x22_slot, saved_x22) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, saved_x23_slot, saved_x23) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xa94257f4, // ldp x20, x21, [sp, #0x20]
        0xaa1303e0, // mov x0, x19
        0xa9414ffe, // ldp x30, x19, [sp, #0x10]
        0xa9435ff6, // ldp x22, x23, [sp, #0x30]
        0x910103ff, // add sp, sp, #0x40
        0xd65f03c0, // ret
    };

    int run_ret = a64_cpu_execute_code_program(&cpu, start_pc, insns,
                                                 sizeof(insns) / sizeof(insns[0]), 6);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = 0;
    if (cpu.pc != return_pc)
        result |= 1ULL;
    if (cpu.x[19] != saved_x19)
        result |= 2ULL;
    if (cpu.x[20] != saved_x20)
        result |= 4ULL;
    if (cpu.x[21] != saved_x21)
        result |= 8ULL;
    if (cpu.x[22] != saved_x22)
        result |= 16ULL;
    if (cpu.x[23] != saved_x23)
        result |= 32ULL;
    if (cpu.x[30] != saved_x30)
        result |= 64ULL;
    if (cpu.sp != stack_ptr + 0x40)
        result |= 128ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_cmp_memory_backed_x27_x23_branches_eq(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[23] = 0x5655ab48ULL;
    cpu.x[27] = 0x5655ab48ULL;

    static const uint32_t insns[] = {
        0xeb17037f, // cmp x27, x23
        0x54000e00, // b.eq 0x6a690
    };

    if (a64_cpu_execute_code_block(&cpu, 0x6a4cc, insns, sizeof(insns) / sizeof(insns[0])) <
        0)
        return UINT64_MAX;

    return cpu.pc;
}

uint64_t tcti_semantic_case_cset_eq_then_add_to_x3(void)
{
    enum {
        A64_CSEL_CSINC = 1,
    };

    void *gadgets[] = {
        (void *)gadget_csel_fallback,
        (void *)3,      // rd
        (void *)31,     // rn: xzr
        (void *)31,     // rm: xzr
        (void *)A64_NE, // cset eq is csinc x3,xzr,xzr,ne
        (void *)A64_CSEL_CSINC,
        (void *)1,
        (void *)gadget_addsub_imm_fallback,
        (void *)3, // rd
        (void *)3, // rn
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

uint64_t tcti_semantic_case_ldp_first_destination_preserves_pair_base(void)
{
    enum {
        base_ptr = 0x140000,
        next_ptr = 0x150000,
    };
    const uint64_t expected_next = next_ptr;
    const uint64_t expected_field = 0x2222333344445555ULL;
    const uint64_t decoy_field = 0x9999aaaabbbbccccULL;

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(next_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[25] = base_ptr;
    cpu.x[1] = 0x1111111111111111ULL;

    if (a64_guest_write64(&cpu, &tlb, base_ptr, expected_next) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, base_ptr + 8, expected_field) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, next_ptr + 8, decoy_field) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t load_pair[] = {
        0xa9400739, // ldp x25, x1, [x25]
    };
    if (a64_cpu_execute_code_block(&cpu, 0x565b3548, load_pair,
                                         sizeof(load_pair) / sizeof(load_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = ((cpu.x[25] == expected_next) ? 0 : 1);
    result |= ((cpu.x[1] == expected_field) ? 0 : 2);

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_ldp_post_index_first_destination_preserves_pair_base(void)
{
    enum {
        base_ptr = 0x160000,
        next_ptr = 0x170000,
    };
    const uint64_t expected_next = next_ptr;
    const uint64_t expected_field = 0x4444555566667777ULL;
    const uint64_t decoy_field = 0xdeadbeefcafebabeULL;

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(next_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[25] = base_ptr;
    cpu.x[1] = 0x1111111111111111ULL;

    if (a64_guest_write64(&cpu, &tlb, base_ptr, expected_next) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, base_ptr + 8, expected_field) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, next_ptr + 8, decoy_field) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t load_pair[] = {
        0xa8c10739, // ldp x25, x1, [x25], #16
    };
    if (a64_cpu_execute_code_block(&cpu, 0x565b3550, load_pair,
                                         sizeof(load_pair) / sizeof(load_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = ((cpu.x[25] == (base_ptr + 16)) ? 0 : 1);
    result |= ((cpu.x[1] == expected_field) ? 0 : 2);

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_ldp_post_index_hot_first_destination_preserves_pair_base(void)
{
    enum {
        base_ptr = 0x158000,
        next_ptr = 0x159000,
    };
    const uint64_t expected_next = next_ptr;
    const uint64_t expected_field = 0x123456789abcdef0ULL;
    const uint64_t decoy_field = 0x0badf00dfeedfaceULL;

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(next_ptr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = base_ptr;
    cpu.x[1] = 0x1111111111111111ULL;

    if (a64_guest_write64(&cpu, &tlb, base_ptr, expected_next) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, base_ptr + 8, expected_field) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, next_ptr + 8, decoy_field) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t load_pair[] = {
        0xa8c10400, // ldp x0, x1, [x0], #16
    };
    if (a64_cpu_execute_code_block(&cpu, 0x565b354c, load_pair,
                                   sizeof(load_pair) / sizeof(load_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = ((cpu.x[0] == (base_ptr + 16)) ? 0 : 1);
    result |= ((cpu.x[1] == expected_field) ? 0 : 2);

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_ldpsw_pair_sign_extends_live_musl_offsets(void)
{
    enum {
        stack_ptr = 0x180000,
        buffer_box = 0x181000,
        buffer_base = 0x210000,
        output_slot = 0x220000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(buffer_box), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(buffer_base), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(output_slot), 1, P_READ | P_WRITE) < 0) {
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
    cpu.x[21] = output_slot; // x2 is already materialized from [x21] before the live block.
    cpu.x[22] = buffer_box;
    cpu.x[2] = output_slot;

    if (a64_guest_write64(&cpu, &tlb, buffer_box, buffer_base) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_ptr + 0x60, 0x20ULL) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, stack_ptr + 0xb0, (uint32_t)-4) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, stack_ptr + 0xb4, 8U) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t block[] = {
        0x695603e1, // ldpsw x1, x0, [sp, #0xb0]
        0xf94033e3, // ldr x3, [sp, #0x60]
        0x8b000021, // add x1, x1, x0
        0xf94002c0, // ldr x0, [x22]
        0x8b010000, // add x0, x0, x1
        0xf9000040, // str x0, [x2]
    };
    if (a64_cpu_execute_code_block(&cpu, 0x462d8, block, sizeof(block) / sizeof(block[0])) <
        0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t stored = 0;
    if (a64_guest_read64(&cpu, &tlb, output_slot, &stored) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 3;
    }

    uint64_t result = 0;
    result |= (cpu.x[1] == 4ULL) ? 0ULL : 1ULL;
    result |= (cpu.x[0] == (buffer_base + 4ULL)) ? 0ULL : 2ULL;
    result |= (stored == (buffer_base + 4ULL)) ? 0ULL : 4ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_ldr_chain_after_memory_backed_x20_alias_base(void)
{
    enum {
        base_ptr = 0x146000,
        pointed_ptr = 0x147000,
    };
    const uint64_t live_value = 0x0123456789abcdefULL;

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
    cpu.x[20] = base_ptr;

    if (a64_guest_write64(&cpu, &tlb, base_ptr + 0xee8, pointed_ptr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, pointed_ptr, live_value) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xf9477694, // ldr x20, [x20, #0xee8]
        0xf9400281, // ldr x1, [x20]
    };
    int run_ret =
        a64_cpu_execute_code_program(&cpu, 0x79790, insns, sizeof(insns) / sizeof(insns[0]), 2);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    if (cpu.x[20] != pointed_ptr)
        result |= 1ULL;
    if (cpu.x[1] != live_value)
        result |= 2ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_ldr_x20_from_memory_backed_x20_alias_base(void)
{
    enum {
        base_ptr = 0x144000,
        pointed_ptr = 0x145000,
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
    cpu.pc = 0x79790;
    cpu.x[20] = base_ptr;

    if (a64_guest_write64(&cpu, &tlb, base_ptr + 0xee8, pointed_ptr) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xf9477694, // ldr x20, [x20, #0xee8]
    };
    if (a64_cpu_execute_code_block(&cpu, 0x79790, insns, sizeof(insns) / sizeof(insns[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = (cpu.x[20] == pointed_ptr) ? 0ULL : cpu.x[20];
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_ldr_x5_from_memory_backed_x27(void)
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

uint64_t tcti_semantic_case_logical_mov_roundtrips_memory_backed_x19(void)
{
    enum {
        A64_LOGICAL_ORR = 1,
    };

    void *gadgets[] = {
        (void *)gadget_logical_reg_fallback,
        (void *)19, // rd: x19, memory-backed
        (void *)31, // rn: xzr for MOV alias
        (void *)3,  // rm: x3
        (void *)A64_SHIFT_LSL,
        (void *)0,
        (void *)A64_LOGICAL_ORR,
        (void *)0,
        (void *)1,
        (void *)gadget_logical_reg_fallback,
        (void *)5,  // rd: x5, hot
        (void *)31, // rn: xzr for MOV alias
        (void *)19, // rm: x19, memory-backed
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

uint64_t tcti_semantic_case_musl_getgrgid_match_path_publishes_result_slot(void)
{
    enum {
        text_base = 0x46390,
        exit_pc = 0x46258,
        stack_ptr = 0x190000,
        buffer_box = 0x191000,
        vector_box = 0x192000,
        group_base = 0x193000,
        vector_base = 0x194000,
        result_slot = 0x195000,
        group_struct = 0x196000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(buffer_box), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(vector_box), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(group_base), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(vector_base), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(result_slot), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(group_struct), 1, P_READ | P_WRITE) < 0) {
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
    cpu.pc = text_base;
    cpu.x[19] = result_slot;
    cpu.x[20] = 0;          // getgrgid path skips strcmp and compares gid directly.
    cpu.x[21] = vector_box;
    cpu.x[22] = buffer_box;
    cpu.x[23] = group_struct;
    cpu.x[26] = 4;          // gid target

    if (a64_guest_write64(&cpu, &tlb, buffer_box, group_base) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_box, vector_base) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, result_slot, 0) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, stack_ptr + 0xb0, 1U) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, stack_ptr + 0xb4, 4U) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, stack_ptr + 0xb8, 4U) != A64_MEM_OK ||
        a64_guest_write8(&cpu, &tlb, group_base + 0, 0) != A64_MEM_OK ||
        a64_guest_write8(&cpu, &tlb, group_base + 1, 'a') != A64_MEM_OK ||
        a64_guest_write8(&cpu, &tlb, group_base + 4, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_base, 0x2222444466668888ULL) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t block[] = {
        0xf94002c1, // ldr x1, [x22]
        0xb980b3e0, // ldrsw x0, [sp, #0xb0]
        0x8b000020, // add x0, x1, x0
        0xa90002e1, // stp x1, x0, [x23]
        0xb940bbe2, // ldr w2, [sp, #0xb8]
        0xb90012e2, // str w2, [x23, #0x10]
        0xf94002a3, // ldr x3, [x21]
        0xf9000ee3, // str x3, [x23, #0x18]
        0x385ff003, // ldurb w3, [x0, #-1]
        0x35fffde3, // cbnz w3, 0x46370
        0xb980b7e3, // ldrsw x3, [sp, #0xb4]
        0x8b030000, // add x0, x0, x3
        0x385ff000, // ldurb w0, [x0, #-1]
        0x35fffd60, // cbnz w0, 0x46370
        0xb40000d4, // cbz x20, 0x463e0
        0x6b1a005f, // cmp w2, w26
        0x54fffc61, // b.ne 0x46370
        0xd503201f, // nop
        0xd503201f, // nop
        0xd503201f, // nop
        0xf9000277, // str x23, [x19]
        0x17ffff9d, // b 0x46250
    };
    if (a64_cpu_execute_code_program(&cpu, text_base, block, sizeof(block) / sizeof(block[0]),
                                           8) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t published = 0;
    uint64_t pair_first = 0;
    uint64_t pair_second = 0;
    uint32_t stored_gid = 0;
    uint64_t stored_vector = 0;
    if (a64_guest_read64(&cpu, &tlb, result_slot, &published) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, group_struct + 0x0, &pair_first) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, group_struct + 0x8, &pair_second) != A64_MEM_OK ||
        a64_guest_read32(&cpu, &tlb, group_struct + 0x10, &stored_gid) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, group_struct + 0x18, &stored_vector) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 3;
    }

    uint64_t result = 0;
    result |= (published == group_struct) ? 0ULL : 1ULL;
    result |= (pair_first == group_base) ? 0ULL : 2ULL;
    result |= (pair_second == (group_base + 1ULL)) ? 0ULL : 4ULL;
    result |= (stored_gid == 4U) ? 0ULL : 8ULL;
    result |= (stored_vector == vector_base) ? 0ULL : 16ULL;
    result |= (cpu.pc == exit_pc) ? 0ULL : 32ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_getgrgid_realloc_tail_publishes_buffer_base(void)
{
    enum {
        text_base = 0x46338,
        exit_pc = 0x46278,
        stack_ptr = 0x1a0000,
        buffer_slot = 0x1a1000,
        size_slot = 0x1a2000,
        new_buffer = 0x1a3000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_ptr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(buffer_slot), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(size_slot), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(new_buffer), 1, P_READ | P_WRITE) < 0) {
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
    cpu.pc = text_base;
    cpu.x[0] = new_buffer;  // realloc return value
    cpu.x[5] = 0x88;        // expanded buffer size
    cpu.x[22] = buffer_slot;
    cpu.x[24] = size_slot;

    if (a64_guest_write64(&cpu, &tlb, stack_ptr + 0x68, cpu.x[5]) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, buffer_slot, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, size_slot, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t block[] = {
        0xf94037e5, // ldr x5, [sp, #0x68]
        0xb4fff860, // cbz x0, 0x46248
        0xf90002c0, // str x0, [x22]
        0xf9000305, // str x5, [x24]
        0xf94002c0, // ldr x0, [x22]
        0x17ffffcb, // b 0x46278
    };
    if (a64_cpu_execute_code_program(&cpu, text_base, block, sizeof(block) / sizeof(block[0]),
                                           8) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t published_base = 0;
    uint64_t published_size = 0;
    if (a64_guest_read64(&cpu, &tlb, buffer_slot, &published_base) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, size_slot, &published_size) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 3;
    }

    uint64_t result = 0;
    result |= (published_base == new_buffer) ? 0ULL : 1ULL;
    result |= (published_size == 0x88ULL) ? 0ULL : 2ULL;
    result |= (cpu.x[0] == new_buffer) ? 0ULL : 4ULL;
    result |= (cpu.pc == exit_pc) ? 0ULL : 8ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_stack_pair_roundtrips_hot_x5_x4(void)
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
    if (a64_cpu_execute_code_block(&cpu, 0x6a298, store_pair,
                                         sizeof(store_pair) / sizeof(store_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    cpu.x[5] = 0xaaaabfff5555ffffULL;
    cpu.x[4] = 0x1111111111111111ULL;

    static const uint32_t load_pair[] = {
        0xa94913e5, // ldp x5, x4, [sp, #0x90]
    };
    if (a64_cpu_execute_code_block(&cpu, 0x6a2ac, load_pair,
                                         sizeof(load_pair) / sizeof(load_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = ((cpu.x[4] == x4_value) ? 0 : 1);
    result |= ((cpu.x[5] == x5_value) ? 0 : 2);

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_stack_pair_stores_memory_backed_x20_x21(void)
{
    enum {
        stack_ptr = 0x131000,
        slot_addr = stack_ptr + 0x20,
    };
    const uint64_t x20_value = 0x1111222233334444ULL;
    const uint64_t x21_value = 0x5555666677778888ULL;

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
    cpu.x[20] = x20_value;
    cpu.x[21] = x21_value;

    static const uint32_t store_pair[] = {
        0xa90257f4, // stp x20, x21, [sp, #0x20]
    };
    if (a64_cpu_execute_code_block(&cpu, 0x7b4d8, store_pair,
                                     sizeof(store_pair) / sizeof(store_pair[0])) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    uint64_t stored_first = 0;
    uint64_t stored_second = 0;
    if (a64_guest_read64(&cpu, &tlb, slot_addr, &stored_first) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, slot_addr + 8, &stored_second) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    if (stored_first != x20_value)
        result |= 1ULL;
    if (stored_second != x21_value)
        result |= 2ULL;
    if (cpu.sp != stack_ptr)
        result |= 4ULL;
    if (cpu.pc != 0x7b4dcULL)
        result |= 8ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_str_x0_to_memory_backed_x22_scaled_x1(void)
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

    uint64_t meta =
        (1ULL << 8) | (1ULL << 16) | ((uint64_t)A64_EXT_LSL << 24) | ((uint64_t)A64_SIZE_X << 32);
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
