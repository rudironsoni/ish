#include "tcti_memory_atomic_runtime_semantic_scenarios.h"

#include "../../Internal/tcti_semantic_runtime_support.h"

uint64_t tcti_semantic_case_busybox_allocator_msub_callback_roundtrip(void)
{
    enum {
        text_base = 0x79fa4,
        callback_pc = 0x150000,
        exit_pc = 0x160000,
        state_addr = 0x170000,
        stack_top = 0x180000,
        stack_page = stack_top - PAGE_SIZE,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(callback_pc), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(exit_pc), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(state_addr), 1, P_READ | P_WRITE) < 0) {
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
    cpu.sp = stack_top;
    cpu.x[0] = state_addr;
    cpu.x[1] = 7;
    cpu.x[30] = exit_pc;

    if (a64_guest_write64(&cpu, &tlb, state_addr + 0x48, callback_pc) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, state_addr + 0x50, 100) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, state_addr + 0x58, 7) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, state_addr + 0x4, 0x1234) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, callback_pc, 0xd65f03c0U) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, exit_pc, 0xd65f03c0U) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (unsigned step = 0; step < 64 && cpu.pc != exit_pc; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x79fa4: insn = 0xa9be4ffe; break; // stp x30, x19, [sp, #-0x20]!
        case 0x79fa8: insn = 0x2a0103e1; break; // mov w1, w1
        case 0x79fac: insn = 0xaa0003f3; break; // mov x19, x0
        case 0x79fb0: insn = 0xa9448803; break; // ldp x3, x2, [x0, #0x48]
        case 0x79fb4: insn = 0x9ac10c40; break; // sdiv x0, x2, x1
        case 0x79fb8: insn = 0x9b018800; break; // msub x0, x0, x1, x2
        case 0x79fbc: insn = 0xcb000020; break; // sub x0, x1, x0
        case 0x79fc0: insn = 0x9ac10c02; break; // sdiv x2, x0, x1
        case 0x79fc4: insn = 0x1b018041; break; // msub w1, w2, w1, w0
        case 0x79fc8: insn = 0xb9400660; break; // ldr w0, [x19, #0x4]
        case 0x79fcc: insn = 0xf9000fe1; break; // str x1, [sp, #0x18]
        case 0x79fd0: insn = 0xd63f0060; break; // blr x3
        case callback_pc: insn = 0xd65f03c0; break; // ret
        case 0x79fd4: insn = 0xf9400fe1; break; // ldr x1, [sp, #0x18]
        case 0x79fd8: insn = 0xf9402a60; break; // ldr x0, [x19, #0x50]
        case 0x79fdc: insn = 0x8b010000; break; // add x0, x0, x1
        case 0x79fe0: insn = 0xf9002a60; break; // str x0, [x19, #0x50]
        case 0x79fe4: insn = 0xa8c24ffe; break; // ldp x30, x19, [sp], #0x20
        case 0x79fe8: insn = 0xd65f03c0; break; // ret
        default:
            mem_destroy(&mem);
            return 0x2000000000000000ULL | cpu.pc;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x3000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (old_pc & 0x0000ffffffffffffULL);
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != exit_pc) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | cpu.pc;
    }

    uint64_t accumulator = UINT64_MAX;
    if (a64_guest_read64(&cpu, &tlb, state_addr + 0x50, &accumulator) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    if (accumulator != 105)
        result |= 1;
    if (cpu.x[0] != 105)
        result |= 2;
    if (cpu.x[1] != 5)
        result |= 4;
    if (cpu.x[19] != 0)
        result |= 8;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_busybox_input_widechar_copy_loop_roundtrip(void)
{
    enum {
        input_page = 0x190000,
        input_addr = input_page + 1,
        stack_top = 0x1a0000,
        stack_page = stack_top - PAGE_SIZE,
        max_chars = 36,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(input_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x5bc90;
    cpu.sp = stack_top;
    cpu.x[0] = input_addr;
    cpu.x[1] = stack_top + 8;
    cpu.x[2] = 0;
    cpu.x[3] = 0;

    for (uint64_t i = 0; i < max_chars; i++) {
        if (a64_guest_write16(&cpu, &tlb, input_addr + (i * 2), (uint16_t)(0x41 + i)) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }
    }

    unsigned step = 0;
    for (; step < 512 && cpu.pc != 0x5bcb4; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x5bc90: insn = 0xd2800002; break; // mov x2, #0
        case 0x5bc94: insn = 0x78627803; break; // ldrh w3, [x0, x2, lsl #1]
        case 0x5bc98: insn = 0x340000a3; break; // cbz w3, 0x5bcac
        case 0x5bc9c: insn = 0xb8227823; break; // str w3, [x1, x2, lsl #2]
        case 0x5bca0: insn = 0x91000442; break; // add x2, x2, #1
        case 0x5bca4: insn = 0xf100905f; break; // cmp x2, #0x24
        case 0x5bca8: insn = 0x54ffff61; break; // b.ne 0x5bc94
        case 0x5bcac: insn = 0x910283e0; break; // add x0, sp, #0xa0
        case 0x5bcb0: insn = 0xb822d83f; break; // str wzr, [x1, w2, sxtw #2]
        default:
            mem_destroy(&mem);
            return 0x1000000000000000ULL | cpu.pc;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (old_pc & 0x0000ffffffffffffULL);
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != 0x5bcb4) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | ((uint64_t)step << 32) | (cpu.pc & 0xffffffffULL);
    }

    uint64_t result = 0;
    for (uint64_t i = 0; i < max_chars; i++) {
        uint32_t copied = 0;
        if (a64_guest_read32(&cpu, &tlb, stack_top + 8 + (i * 4), &copied) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
        if (copied != (uint32_t)(0x41 + i))
            result |= 1ULL;
    }

    uint32_t terminator = UINT32_MAX;
    if (a64_guest_read32(&cpu, &tlb, stack_top + 8 + (max_chars * 4), &terminator) !=
        A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 3;
    }

    if (terminator != 0)
        result |= 2ULL;
    if (cpu.x[2] != max_chars)
        result |= 4ULL;
    if (cpu.x[0] != stack_top + 0xa0)
        result |= 8ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_busybox_ls_retry_ccmp_close_path(void)
{
    enum {
        text_base = 0x1f8a8,
        retry_pc = 0x1f894,
        fallthrough_pc = 0x1f8c4,
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        flags_slot = stack_top - 4,
    };

    static const uint32_t insns[] = {
        0x7100001f, // cmp w0, #0
        0xb9400fe3, // ldr w3, [sp, #0xc]
        0xaa0003f5, // mov x21, x0
        0x2a0003f4, // mov w20, w0
        0x7a401864, // ccmp w3, #0, #4, ne
        0x52800003, // mov w3, #0
        0x54fffea1, // b.ne 0x1f894
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    uint64_t result = 0;
    struct {
        uint32_t w0;
        uint32_t saved_w3;
        uint64_t expected_pc;
        uint32_t expected_x21;
    } cases[] = {
        { .w0 = 0, .saved_w3 = 4, .expected_pc = fallthrough_pc, .expected_x21 = 0 },
        { .w0 = UINT32_MAX, .saved_w3 = 4, .expected_pc = retry_pc, .expected_x21 = UINT32_MAX },
        { .w0 = UINT32_MAX, .saved_w3 = 0, .expected_pc = fallthrough_pc, .expected_x21 = UINT32_MAX },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct cpu_state cpu;
        memset(&cpu, 0, sizeof(cpu));
        cpu.mmu = &mem.mmu;
        cpu.tlb = &tlb;
        cpu.pc = text_base;
        cpu.sp = stack_top - 16;
        cpu.x[0] = cases[i].w0;

        if (a64_guest_write32(&cpu, &tlb, flags_slot, cases[i].saved_w3) != A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 1;
        }

        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, insns,
                                                       sizeof(insns) / sizeof(insns[0]));
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | i;
        }

        if (cpu.pc != cases[i].expected_pc)
            result |= 1ULL << (i * 4);
        if ((uint32_t)cpu.x[21] != cases[i].expected_x21)
            result |= 2ULL << (i * 4);
        if ((uint32_t)cpu.x[20] != cases[i].w0)
            result |= 4ULL << (i * 4);
        if ((uint32_t)cpu.x[3] != 0)
            result |= 8ULL << (i * 4);
    }

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_busybox_scandir_flatten_block_roundtrip(void)
{
    enum {
        array_base = 0x180000,
        node0_addr = 0x181000,
        node1_addr = 0x182000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(array_base), 3, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x565e10cc;
    cpu.x[0] = array_base; // x0 becomes x19, then w0 is cleared for slot 0.
    cpu.x[20] = node0_addr; // live path stores the current list node from memory-backed x20.

    if (a64_guest_write64(&cpu, &tlb, array_base, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, array_base + 8, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, node0_addr + 16, node1_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, node1_addr + 16, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xaa0003f3, // mov x19, x0
        0x52800000, // mov w0, #0
        0xf8205a74, // str x20, [x19, w0, uxtw #3]
        0xf9400a94, // ldr x20, [x20, #16]
        0xb4000014, // cbz x20, +0x8 (not taken in this harness)
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t slot0_value = 0;
    uint64_t slot1_value = 0;
    if (a64_guest_read64(&cpu, &tlb, array_base, &slot0_value) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, array_base + 8, &slot1_value) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    result |= (slot0_value == node0_addr) ? 0
                                          : (0x2000000000000000ULL |
                                             (slot0_value & 0x0fffffffffffffffULL));
    result |= (slot1_value == 0) ? 0
                                 : (0x3000000000000000ULL |
                                    (slot1_value & 0x0fffffffffffffffULL));
    result |= (cpu.x[19] == array_base) ? 0
                                        : (0x4000000000000000ULL |
                                           (cpu.x[19] & 0x0fffffffffffffffULL));
    result |= (cpu.x[20] == node1_addr) ? 0
                                        : (0x5000000000000000ULL |
                                           (cpu.x[20] & 0x0fffffffffffffffULL));
    result |= (cpu.x[0] == 0) ? 0 : (0x6000000000000000ULL | (cpu.x[0] & 0x0fffffffffffffffULL));

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_generated_ldp_x2_x0_bytecode_matches_manual_shape(void)
{
    struct mem mem;
    mem_init(&mem);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf48;

    if (a64_cpu_install_code(&cpu, 0x7bf48, (const uint32_t[]){0xa94082a2U}, 1) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct a64_block *block = a64_compile_block(&cpu, 0x7bf48, &tlb);
    if (!block) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    tcti_gadget_t *gadgets = block->gadgets;
    size_t num_gadgets = block->num_gadgets;

    uint64_t result = 0;
    if (num_gadgets != 19)
        result |= 1ULL << 0;
    if (gadgets[0] != (tcti_gadget_t)gadget_ldr_x)
        result |= 1ULL << 1;
    if ((uint64_t)(uintptr_t)gadgets[1] != 0x7bf48)
        result |= 1ULL << 2;
    if ((uint64_t)(uintptr_t)gadgets[2] != 2)
        result |= 1ULL << 3;
    if ((uint64_t)(uintptr_t)gadgets[3] != 21)
        result |= 1ULL << 4;
    if ((uint64_t)(uintptr_t)gadgets[4] != 8)
        result |= 1ULL << 5;
    if ((uint64_t)(uintptr_t)gadgets[5] != 3)
        result |= 1ULL << 6;
    if ((uint64_t)(uintptr_t)gadgets[6] != 0)
        result |= 1ULL << 7;
    if ((uint64_t)(uintptr_t)gadgets[7] != (1ULL << 40))
        result |= 1ULL << 8;
    if (gadgets[8] != (tcti_gadget_t)gadget_ldr_x)
        result |= 1ULL << 9;
    if ((uint64_t)(uintptr_t)gadgets[9] != 0x7bf48)
        result |= 1ULL << 10;
    if ((uint64_t)(uintptr_t)gadgets[10] != 0)
        result |= 1ULL << 11;
    if ((uint64_t)(uintptr_t)gadgets[11] != 21)
        result |= 1ULL << 12;
    if ((uint64_t)(uintptr_t)gadgets[12] != 16)
        result |= 1ULL << 13;
    if ((uint64_t)(uintptr_t)gadgets[13] != 3)
        result |= 1ULL << 14;
    if ((uint64_t)(uintptr_t)gadgets[14] != 0)
        result |= 1ULL << 15;
    if ((uint64_t)(uintptr_t)gadgets[15] != (1ULL << 40))
        result |= 1ULL << 16;
    if (gadgets[16] != (tcti_gadget_t)gadget_pc_advance)
        result |= 1ULL << 17;
    if ((uint64_t)(uintptr_t)gadgets[17] != 0x7bf4c)
        result |= 1ULL << 18;
    if (gadgets[18] != (tcti_gadget_t)gadget_exit)
        result |= 1ULL << 19;

    a64_block_free(block);
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_generated_ldr_w_reg_offset_reads_hot_x0_offset(void)
{
    enum {
        base_addr = 0x015d38,
        load_offset = 0x5040,
        expected_word = 0x11223344U,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_addr), 8, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7a1cc;
    cpu.x[0] = load_offset;
    cpu.x[20] = base_addr;

    if (a64_guest_write32(&cpu, &tlb, base_addr + load_offset, expected_word) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insn = 0xb8606a87; // ldr w7, [x20, x0]
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }
    if (cpu.x[7] != expected_word) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | cpu.x[7];
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_generated_reg_offset_ldr_x0_alias_base_reads_expected_qword(void)
{
    enum {
        table_addr = 0xe0000,
        expected_qword = 0x17,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(table_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf54;
    cpu.x[0] = table_addr;
    cpu.x[25] = 8;

    if (a64_guest_write64(&cpu, &tlb, table_addr + 8, expected_qword) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insn = 0xf8796800; // ldr x0, [x0, x25]
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = cpu.x[0];
    mem_destroy(&mem);
    return result == expected_qword ? 0ULL : (0x4000000000000000ULL | result);
}

uint64_t tcti_semantic_case_ldp_x2_x0_from_memory_backed_x21(void)
{
    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, 0xd2000 >> PAGE_BITS, 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf48;
    cpu.x[21] = 0xd2908;

    if (a64_guest_write64(&cpu, &tlb, 0xd2910, 0x55) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, 0xd2918, 0xe0000) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insn = 0xa94082a2; // ldp x2, x0, [x21, #8]
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = 0;
    if (cpu.x[0] != 0xe0000)
        result |= 1;
    if (cpu.x[2] != 0x55)
        result |= 2;
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_ldr_x0_imm8_from_memory_backed_x21(void)
{
    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, 0xd2000 >> PAGE_BITS, 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf48;
    cpu.x[21] = 0xd2908;

    if (a64_guest_write64(&cpu, &tlb, 0xd2918, 0xe0000) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insn = 0xf9400aa0; // ldr x0, [x21, #16]
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = cpu.x[0];
    mem_destroy(&mem);
    return result == 0xe0000 ? 0ULL : (0x2000000000000000ULL | result);
}

uint64_t tcti_semantic_case_ldr_x1_imm_from_memory_backed_x21(void)
{
    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, 0xd2000 >> PAGE_BITS, 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf50;
    cpu.x[21] = 0xd2908;

    if (a64_guest_write64(&cpu, &tlb, 0xd2968, 0x39) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insn = 0xf94032a1; // ldr x1, [x21, #96]
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = cpu.x[1];
    mem_destroy(&mem);
    return result == 0x39 ? 0ULL : (0x2000000000000000ULL | result);
}

uint64_t tcti_semantic_case_ldr_x2_imm0_from_memory_backed_x21(void)
{
    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, 0xd2000 >> PAGE_BITS, 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf48;
    cpu.x[21] = 0xd2908;

    if (a64_guest_write64(&cpu, &tlb, 0xd2910, 0x55) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insn = 0xf94006a2; // ldr x2, [x21, #8]
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = cpu.x[2];
    mem_destroy(&mem);
    return result == 0x55 ? 0ULL : (0x2000000000000000ULL | result);
}

uint64_t tcti_semantic_case_logical_mov_memory_to_memory_roundtrip(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d380;
    cpu.x[19] = 0x1111111111111111ULL;
    cpu.x[28] = 0x40000ab0ULL;

    static const uint32_t mov_x19_x28 = 0xaa1c03f3; // mov x19, x28
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &mov_x19_x28, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.x[19] != 0x40000ab0ULL)
        return 0x2000000000000000ULL | cpu.x[19];
    if (cpu.x[28] != 0x40000ab0ULL)
        return 0x3000000000000000ULL | cpu.x[28];

    cpu.pc = 0x6d4b0;
    cpu.x[19] = 0x40000a68ULL;
    cpu.x[28] = 0x2222222222222222ULL;

    static const uint32_t mov_x28_x19 = 0xaa1303fc; // mov x28, x19
    run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &mov_x28_x19, 1);
    if (run_ret < 0)
        return 0x4000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.x[28] != 0x40000a68ULL)
        return 0x5000000000000000ULL | cpu.x[28];
    if (cpu.x[19] != 0x40000a68ULL)
        return 0x6000000000000000ULL | cpu.x[19];

    return 0;
}

uint64_t tcti_semantic_case_lslv_64bit_uses_full_shift_amount(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x7c068;
    cpu.x[0] = 35;
    cpu.x[5] = 1;

    static const uint32_t insn = 0x9ac020a3; // lsl x3, x5, x0
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    return cpu.x[3];
}

uint64_t tcti_semantic_case_lslv_memory_backed_registers_roundtrip(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x79f70;
    cpu.x[13] = 35;
    cpu.x[19] = 1;

    static const uint32_t insn = 0x9acd2273; // lsl x19, x19, x13
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    return cpu.x[19];
}

uint64_t tcti_semantic_case_manual_two_ldr_shared_block_from_memory_backed_x21(void)
{
    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, 0xd2000 >> PAGE_BITS, 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf48;
    cpu.x[21] = 0xd2908;

    if (a64_guest_write64(&cpu, &tlb, 0xd2910, 0x55) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, 0xd2918, 0xe0000) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    void *gadgets[] = {
        (void *)gadget_ldr_x, (void *)0x7bf48, (void *)2, (void *)21, (void *)8, (void *)3,
        (void *)0,            (void *)0,
        (void *)gadget_ldr_x, (void *)0x7bf48, (void *)0, (void *)21, (void *)16, (void *)3,
        (void *)0,            (void *)0,
        (void *)gadget_exit,
    };

    tcti_entry_block(gadgets, &cpu);

    uint64_t result = 0;
    if (cpu.x[0] != 0xe0000)
        result |= 1;
    if (cpu.x[2] != 0x55)
        result |= 2;
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_manual_two_ldr_shared_block_with_pc_advance(void)
{
    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, 0xd2000 >> PAGE_BITS, 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf48;
    cpu.x[21] = 0xd2908;

    if (a64_guest_write64(&cpu, &tlb, 0xd2910, 0x55) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, 0xd2918, 0xe0000) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    void *gadgets[] = {
        (void *)gadget_ldr_x,      (void *)0x7bf48, (void *)2, (void *)21, (void *)8, (void *)3,
        (void *)0,                 (void *)0,
        (void *)gadget_ldr_x,      (void *)0x7bf48, (void *)0, (void *)21, (void *)16, (void *)3,
        (void *)0,                 (void *)0,
        (void *)gadget_pc_advance, (void *)0x7bf4c,
        (void *)gadget_exit,
    };

    tcti_entry_block(gadgets, &cpu);

    uint64_t result = 0;
    if (cpu.x[0] != 0xe0000)
        result |= 1;
    if (cpu.x[2] != 0x55)
        result |= 2;
    if (cpu.pc != 0x7bf4c)
        result |= 4;
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_movz_smull_prefix_preserves_expected_x0(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x7a1c0;
    cpu.x[0] = 0x358;
    cpu.x[1] = 0x23488;

    static const uint32_t insns[] = {
        0x52800301, // mov w1, #24
        0x9b217c00, // smull x0, w0, w1
    };

    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, insns,
                                                   sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.x[1] != 24ULL)
        return 0x2000000000000000ULL | cpu.x[1];
    if (cpu.x[0] != 0x5040ULL)
        return 0x3000000000000000ULL | cpu.x[0];
    return 0;
}

uint64_t tcti_semantic_case_musl_bucket_bitmask_block_roundtrip(void)
{
    enum {
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        expected = 1ULL << 35,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
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
    cpu.x[0] = 35;
    cpu.x[5] = 1;

    static const uint32_t insns[] = {
        0xf94027e2, // ldr x2, [sp, #72]
        0x9ac020a3, // lsl x3, x5, x0
        0xaa030042, // orr x2, x2, x3
        0xf90027e2, // str x2, [sp, #72]
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, 0x7c064, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t stored = 0;
    if (a64_guest_read64(&cpu, &tlb, stack_top + 72, &stored) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    uint64_t result = 0;
    result |= (stored == expected) ? 0 : (0x2000000000000000ULL | (stored & 0x0fffffffffffffffULL));
    result |= (cpu.x[2] == expected) ? 0 : (0x3000000000000000ULL | (cpu.x[2] & 0x0fffffffffffffffULL));
    result |= (cpu.x[3] == expected) ? 0 : (0x4000000000000000ULL | (cpu.x[3] & 0x0fffffffffffffffULL));
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_callback_prefix_materializes_args(void)
{
    enum {
        text_base = 0x7bf48,
        exit_pc = 0x7bf60,
        state_addr = 0xd2908,
        dso_addr = 0xd0000,
        table_addr = 0xe0000,
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(state_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(dso_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(table_addr), 1, P_READ | P_WRITE) < 0) {
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
    cpu.sp = stack_top;
    cpu.x[21] = state_addr;
    cpu.x[23] = dso_addr;
    cpu.x[25] = 8;
    cpu.x[26] = 0xaf898;

    if (a64_guest_write64(&cpu, &tlb, state_addr + 8, 0x55) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, state_addr + 16, table_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, state_addr + 96, 0x39) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, table_addr + 8, 0x17) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 976, 0xdc730) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (unsigned step = 0; step < 16 && cpu.pc != exit_pc; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x7bf48: insn = 0xa94082a2; break; // ldp x2, x0, [x21, #8]
        case 0x7bf4c: insn = 0xf941eae3; break; // ldr x3, [x23, #976]
        case 0x7bf50: insn = 0xf94032a1; break; // ldr x1, [x21, #96]
        case 0x7bf54: insn = 0xf8796800; break; // ldr x0, [x0, x25]
        case 0x7bf58: insn = 0x8b000021; break; // add x1, x1, x0
        case 0x7bf5c: insn = 0xaa1a03e0; break; // mov x0, x26
        default:
            mem_destroy(&mem);
            return 0x1000000000000000ULL | cpu.pc;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (old_pc & 0x0000ffffffffffffULL);
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != exit_pc) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | (cpu.pc & 0x0000ffffffffffffULL);
    }

    uint64_t result = 0;
    if (cpu.x[0] != 0xaf898)
        result |= 1;
    if (cpu.x[1] != 0x50)
        result |= 2;
    if (cpu.x[2] != 0x55)
        result |= 4;
    if (cpu.x[3] != 0xdc730)
        result |= 8;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_callback_slot_loads_branch_target(void)
{
    enum {
        text_base = 0x7bf44,
        callback_pc = 0xdc730,
        exit_pc = 0x7bf64,
        state_addr = 0xd2908,
        dso_addr = 0xd0000,
        table_addr = 0xe0000,
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(state_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(dso_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(table_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(callback_pc), 1, P_READ | P_WRITE) < 0) {
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
    cpu.sp = stack_top;
    cpu.x[0] = 0;
    cpu.x[21] = state_addr;
    cpu.x[23] = dso_addr;
    cpu.x[25] = 8;
    cpu.x[26] = 0xaf898;

    if (a64_guest_write64(&cpu, &tlb, state_addr + 8, 0x55) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, state_addr + 16, table_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, state_addr + 96, 0x39) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, table_addr + 8, 0x17) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dso_addr + 976, callback_pc) != A64_MEM_OK ||
        a64_guest_write32(&cpu, &tlb, callback_pc, 0xd65f03c0U) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (unsigned step = 0; step < 32 && cpu.pc != exit_pc; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x7bf44: insn = 0xb5fffe20; break; // cbnz x0, 0x7bf08
        case 0x7bf48: insn = 0xa94082a2; break; // ldp x2, x0, [x21, #8]
        case 0x7bf4c: insn = 0xf941eae3; break; // ldr x3, [x23, #976]
        case 0x7bf50: insn = 0xf94032a1; break; // ldr x1, [x21, #96]
        case 0x7bf54: insn = 0xf8796800; break; // ldr x0, [x0, x25]
        case 0x7bf58: insn = 0x8b000021; break; // add x1, x1, x0
        case 0x7bf5c: insn = 0xaa1a03e0; break; // mov x0, x26
        case 0x7bf60: insn = 0xd63f0060; break; // blr x3
        case callback_pc: insn = 0xd65f03c0; break; // ret
        default:
            mem_destroy(&mem);
            return 0x1000000000000000ULL | cpu.pc;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (old_pc & 0x0000ffffffffffffULL);
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != exit_pc) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | (cpu.pc & 0x0000ffffffffffffULL);
    }

    uint64_t result = 0;
    if (cpu.x[0] != 0xaf898)
        result |= 1;
    if (cpu.x[1] != 0x50)
        result |= 2;
    if (cpu.x[2] != 0x55)
        result |= 4;
    if (cpu.x[3] != callback_pc)
        result |= 8;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_callback_table_walk_materializes_dispatch_args(void)
{
    enum {
        text_base = 0x7bf14,
        exit_pc = 0x7bf40,
        state_addr = 0xd2908,
        table_addr = 0xe0000,
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(state_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(table_addr), 1, P_READ | P_WRITE) < 0) {
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
    cpu.sp = stack_top;
    cpu.x[19] = 0;
    cpu.x[21] = state_addr;

    if (a64_guest_write64(&cpu, &tlb, state_addr + 16, table_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, state_addr + 96, 0x39) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, table_addr + 16, 1) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, table_addr + 24, 0x17) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (unsigned step = 0; step < 16 && cpu.pc != exit_pc; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x7bf14: insn = 0xf9400aa1; break; // ldr x1, [x21, #16]
        case 0x7bf18: insn = 0x91004273; break; // add x19, x19, #16
        case 0x7bf1c: insn = 0xf8736834; break; // ldr x20, [x1, x19]
        case 0x7bf20: insn = 0xb40002f4; break; // cbz x20, 0x7bf7c
        case 0x7bf24: insn = 0xf100069f; break; // cmp x20, #1
        case 0x7bf28: insn = 0x54ffff81; break; // b.ne 0x7bf18
        case 0x7bf2c: insn = 0x91002279; break; // add x25, x19, #8
        case 0x7bf30: insn = 0xf94032a2; break; // ldr x2, [x21, #96]
        case 0x7bf34: insn = 0xf8796820; break; // ldr x0, [x1, x25]
        case 0x7bf38: insn = 0xaa1503e1; break; // mov x1, x21
        case 0x7bf3c: insn = 0x8b000040; break; // add x0, x2, x0
        default:
            mem_destroy(&mem);
            return 0x1000000000000000ULL | cpu.pc;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (old_pc & 0x0000ffffffffffffULL);
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != exit_pc) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | (cpu.pc & 0x0000ffffffffffffULL);
    }

    uint64_t result = 0;
    if (cpu.x[0] != 0x50)
        result |= 1;
    if (cpu.x[1] != state_addr)
        result |= 2;
    if (cpu.x[2] != 0x39)
        result |= 4;
    if (cpu.x[19] != 0x10)
        result |= 8;
    if (cpu.x[20] != 1)
        result |= 16;
    if (cpu.x[25] != 0x18)
        result |= 32;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_frame_stride_block_uses_wide_immediates_and_reg_offset_ldr(void)
{
    enum {
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        base_addr = 0x015d38,
        load_offset = 0x5040,
        expected_word = 0x11223344U,
        expected_saved = 0x5555666677778888ULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(base_addr), 8, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7a1c0;
    cpu.sp = stack_top;
    cpu.x[0] = 0x358;
    cpu.x[1] = 0x23488;
    cpu.x[7] = 0x7b001;
    cpu.x[20] = base_addr;

    if (a64_guest_write32(&cpu, &tlb, base_addr + load_offset, expected_word) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x68, expected_saved) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0x52800301, // mov w1, #24
        0x9b217c00, // smull x0, w0, w1
        0x8b00028e, // add x14, x20, x0
        0xb8606a87, // ldr w7, [x20, x0]
        0xf94037e0, // ldr x0, [sp, #0x68]
        0x8b070019, // add x25, x0, x7
    };

    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, insns,
                                                   sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    if (cpu.x[1] != 24ULL) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | cpu.x[1];
    }
    if (cpu.x[0] != expected_saved) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | cpu.x[0];
    }
    if (cpu.x[7] != expected_word) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | cpu.x[7];
    }
    if (cpu.x[14] != (base_addr + load_offset)) {
        mem_destroy(&mem);
        return 0x5000000000000000ULL | cpu.x[14];
    }
    if (cpu.x[25] != (expected_saved + expected_word)) {
        mem_destroy(&mem);
        return 0x6000000000000000ULL | cpu.x[25];
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_musl_frame_stride_prefix_then_ldr_in_next_block(void)
{
    enum {
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        base_addr = 0x015d38,
        load_offset = 0x5040,
        expected_word = 0x11223344U,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(base_addr), 8, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7a1c0;
    cpu.sp = stack_top;
    cpu.x[0] = 0x358;
    cpu.x[1] = 0x23488;
    cpu.x[20] = base_addr;

    if (a64_guest_write32(&cpu, &tlb, base_addr + load_offset, expected_word) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t prefix[] = {
        0x52800301, // mov w1, #24
        0x9b217c00, // smull x0, w0, w1
        0x8b00028e, // add x14, x20, x0
    };
    static const uint32_t ldr_insn = 0xb8606a87; // ldr w7, [x20, x0]

    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, prefix,
                                                   sizeof(prefix) / sizeof(prefix[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }
    if (cpu.x[0] != load_offset) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | cpu.x[0];
    }
    if (cpu.x[14] != (base_addr + load_offset)) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | cpu.x[14];
    }

    cpu.pc = 0x7a1cc;
    run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &ldr_insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }
    if (cpu.x[7] != expected_word) {
        mem_destroy(&mem);
        return 0x5000000000000000ULL | cpu.x[7];
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_musl_ls_long_vector_tail_and_dynamic_tag_scan(void)
{
    enum {
        text_base = 0x6c15c,
        exit_pc = 0x6c1f8,
        stack_top = 0x120000,
        stack_page = stack_top - PAGE_SIZE,
        state_addr = 0x130000,
        fallback_addr = 0x140000,
        global_slot_addr = 0xbf000 + 0xf70,
        rtld_state_addr = 0xc2000 + 0xba0,
        rtld_tag_slot_addr = 0xc2000 + 0xe70,
        literal_addr = 0x9f000 + 0x868,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(state_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(fallback_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(global_slot_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(rtld_state_addr), 1, P_READ | P_WRITE) < 0) {
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
    cpu.sp = stack_top;
    cpu.x[0] = fallback_addr;
    cpu.x[1] = state_addr;

    const uint64_t vector_tail_start = state_addr + 0x18;
    const uint64_t vector_after_null = state_addr + 0x30;
    const uint64_t wanted_tag_value = 0xdeadbeefcafebabeULL;

    if (a64_guest_write64(&cpu, &tlb, state_addr, 1) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_tail_start + 0x0, 0x1111111111111111ULL) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_tail_start + 0x8, 0x2222222222222222ULL) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_tail_start + 0x10, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_after_null + 0x00, 1) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_after_null + 0x08, 0xaaaaaaaaaaaaaaaaULL) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_after_null + 0x10, 6) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, vector_after_null + 0x18, wanted_tag_value) !=
            A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, global_slot_addr, 0) != A64_MEM_OK ||
        a64_guest_write16(&cpu, &tlb, fallback_addr + 0x36, 0x55) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, fallback_addr + 0x20, 0x80) != A64_MEM_OK ||
        a64_guest_write16(&cpu, &tlb, fallback_addr + 0x38, 0x1234) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (unsigned step = 0; step < 128 && cpu.pc != exit_pc; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x6c15c: insn = 0xa9aa7bfd; break; // stp x29, x30, [sp, #-0x160]!
        case 0x6c160: insn = 0x910003fd; break; // mov x29, sp
        case 0x6c164: insn = 0xa90153f3; break; // stp x19, x20, [sp, #0x10]
        case 0x6c168: insn = 0xaa0103f4; break; // mov x20, x1
        case 0x6c16c: insn = 0xa9025bf5; break; // stp x21, x22, [sp, #0x20]
        case 0x6c170: insn = 0xf9400033; break; // ldr x19, [x1]
        case 0x6c174: insn = 0x91000a73; break; // add x19, x19, #0x2
        case 0x6c178: insn = 0x8b130c33; break; // add x19, x1, x19, lsl #3
        case 0x6c17c: insn = 0xf8408661; break; // ldr x1, [x19], #0x8
        case 0x6c180: insn = 0xb5ffffe1; break; // cbnz x1, 0x6c17c
        case 0x6c184: insn = 0xf0000281; break; // adrp x1, 0xbf000
        case 0x6c188: insn = 0xf947b821; break; // ldr x1, [x1, #0xf70]
        case 0x6c18c: insn = 0xd00002b5; break; // adrp x21, 0xc2000
        case 0x6c190: insn = 0x912e82a2; break; // add x2, x21, #0xba0
        case 0x6c194: insn = 0xf0000183; break; // adrp x3, 0x9f000
        case 0x6c198: insn = 0xf100003f; break; // cmp x1, #0x0
        case 0x6c19c: insn = 0x9121a063; break; // add x3, x3, #0x868
        case 0x6c1a0: insn = 0x9a810001; break; // csel x1, x0, x1, eq
        case 0x6c1a4: insn = 0xa9000c40; break; // stp x0, x3, [x2]
        case 0x6c1a8: insn = 0xf900a843; break; // str x3, [x2, #0x150]
        case 0x6c1ac: insn = 0x79407023; break; // ldrh w3, [x1, #0x38]
        case 0x6c1b0: insn = 0xb9003043; break; // str w3, [x2, #0x30]
        case 0x6c1b4: insn = 0xf9401023; break; // ldr x3, [x1, #0x20]
        case 0x6c1b8: insn = 0x8b030000; break; // add x0, x0, x3
        case 0x6c1bc: insn = 0xf9001440; break; // str x0, [x2, #0x28]
        case 0x6c1c0: insn = 0x79406c20; break; // ldrh w0, [x1, #0x36]
        case 0x6c1c4: insn = 0xf9001c40; break; // str x0, [x2, #0x38]
        case 0x6c1c8: insn = 0xaa1303e0; break; // mov x0, x19
        case 0x6c1cc: insn = 0x14000002; break; // b 0x6c1d4
        case 0x6c1d0: insn = 0x91004000; break; // add x0, x0, #0x10
        case 0x6c1d4: insn = 0xf9400001; break; // ldr x1, [x0]
        case 0x6c1d8: insn = 0xf100183f; break; // cmp x1, #0x6
        case 0x6c1dc: insn = 0x54000060; break; // b.eq 0x6c1e8
        case 0x6c1e0: insn = 0xb5ffff81; break; // cbnz x1, 0x6c1d0
        case 0x6c1e4: insn = 0x14000004; break; // b 0x6c1f4
        case 0x6c1e8: insn = 0xf9400401; break; // ldr x1, [x0, #0x8]
        case 0x6c1ec: insn = 0xd00002a0; break; // adrp x0, 0xc2000
        case 0x6c1f0: insn = 0xf9073801; break; // str x1, [x0, #0xe70]
        case 0x6c1f4: insn = 0x912e82a0; break; // add x0, x21, #0xba0
        default:
            mem_destroy(&mem);
            return 0x1000000000000000ULL | (cpu.pc & 0x0000ffffffffffffULL);
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x2000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (old_pc & 0x0000ffffffffffffULL);
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != exit_pc) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | (cpu.pc & 0x0000ffffffffffffULL);
    }

    uint64_t result = 0;
    uint64_t qword = 0;
    uint32_t word = 0;

    if (cpu.x[19] != vector_after_null)
        result |= 1ULL << 0;
    if (cpu.x[20] != state_addr)
        result |= 1ULL << 1;
    if (cpu.x[21] != 0xc2000)
        result |= 1ULL << 2;
    if (cpu.x[0] != rtld_state_addr)
        result |= 1ULL << 3;
    if (cpu.x[1] != wanted_tag_value)
        result |= 1ULL << 4;

    if (a64_guest_read64(&cpu, &tlb, rtld_tag_slot_addr, &qword) != A64_MEM_OK || qword != wanted_tag_value)
        result |= 1ULL << 5;
    if (a64_guest_read64(&cpu, &tlb, rtld_state_addr + 0x00, &qword) != A64_MEM_OK || qword != fallback_addr)
        result |= 1ULL << 6;
    if (a64_guest_read64(&cpu, &tlb, rtld_state_addr + 0x08, &qword) != A64_MEM_OK || qword != literal_addr)
        result |= 1ULL << 7;
    if (a64_guest_read64(&cpu, &tlb, rtld_state_addr + 0x28, &qword) != A64_MEM_OK || qword != fallback_addr + 0x80)
        result |= 1ULL << 8;
    if (a64_guest_read32(&cpu, &tlb, rtld_state_addr + 0x30, &word) != A64_MEM_OK || word != 0x1234)
        result |= 1ULL << 9;
    if (a64_guest_read64(&cpu, &tlb, rtld_state_addr + 0x38, &qword) != A64_MEM_OK || qword != 0x55)
        result |= 1ULL << 10;
    if (a64_guest_read64(&cpu, &tlb, rtld_state_addr + 0x150, &qword) != A64_MEM_OK || qword != literal_addr)
        result |= 1ULL << 11;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_ls_root_post_open_bucket_loop(void)
{
    enum {
        text_base = 0x7c028,
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        node_addr = 0x140000,
        expected_bucket = 35,
        expected_entry = 0xfeedfacecafebeefULL,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(node_addr), 1, P_READ | P_WRITE) < 0) {
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
    cpu.sp = stack_top;
    cpu.x[19] = node_addr;

    if (a64_guest_write64(&cpu, &tlb, node_addr + 16, node_addr + 0x0f0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, node_addr + 0x0f0, expected_bucket) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, node_addr + 0x0f8, expected_entry) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, node_addr + 0x100, 0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, node_addr + 0x108, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    unsigned step = 0;
    for (; step < 256 && cpu.pc != 0x7c080; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x7c028: insn = 0xf9400a61; break; // ldr x1, [x19, #16]
        case 0x7c02c: insn = 0x910123e4; break; // add x4, sp, #72
        case 0x7c030: insn = 0xaa0403e0; break; // mov x0, x4
        case 0x7c034: insn = 0xf800841f; break; // str xzr, [x0], #8
        case 0x7c038: insn = 0x9105c3e2; break; // add x2, sp, #368
        case 0x7c03c: insn = 0xeb00005f; break; // cmp x2, x0
        case 0x7c040: insn = 0x54ffffa1; break; // b.ne 0x7c034
        case 0x7c044: insn = 0xd2800025; break; // mov x5, #1
        case 0x7c048: insn = 0x14000002; break; // b 0x7c050
        case 0x7c04c: insn = 0x91004021; break; // add x1, x1, #16
        case 0x7c050: insn = 0xf9400020; break; // ldr x0, [x1]
        case 0x7c054: insn = 0xb4000160; break; // cbz x0, 0x7c080
        case 0x7c058: insn = 0xd1000402; break; // sub x2, x0, #1
        case 0x7c05c: insn = 0xf1008c5f; break; // cmp x2, #35
        case 0x7c060: insn = 0x54ffff68; break; // b.hi 0x7c04c
        case 0x7c064: insn = 0xf94027e2; break; // ldr x2, [sp, #72]
        case 0x7c068: insn = 0x9ac020a3; break; // lsl x3, x5, x0
        case 0x7c06c: insn = 0xaa030042; break; // orr x2, x2, x3
        case 0x7c070: insn = 0xf90027e2; break; // str x2, [sp, #72]
        case 0x7c074: insn = 0xf9400422; break; // ldr x2, [x1, #8]
        case 0x7c078: insn = 0xf8207882; break; // str x2, [x4, x0, lsl #3]
        case 0x7c07c: insn = 0x17fffff4; break; // b 0x7c04c
        default:
            mem_destroy(&mem);
            return 0x5000000000000000ULL | cpu.pc;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (cpu.pc & 0x0000ffffffffffffULL);
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != 0x7c080) {
        mem_destroy(&mem);
        return 0x6000000000000000ULL | ((uint64_t)step << 32) | (cpu.pc & 0xffffffffULL);
    }

    uint64_t bitmask = 0;
    uint64_t table_value = 0;
    if (a64_guest_read64(&cpu, &tlb, stack_top + 72, &bitmask) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, stack_top + 72 + (expected_bucket * 8), &table_value) !=
            A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    if (bitmask != (1ULL << expected_bucket)) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | (bitmask & 0x0fffffffffffffffULL);
    }
    if (table_value != expected_entry) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | (table_value & 0x0fffffffffffffffULL);
    }
    if (cpu.pc != 0x7c080) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | cpu.pc;
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_musl_ls_root_post_open_callback_scan(void)
{
    enum {
        text_base = 0x7c080,
        loop_head = 0x7c0a0,
        loop_call = 0x7c0a8,
        loop_exit = 0x7c0b0,
        restart_pc = 0x7c000,
        callback_pc = 0xdc730,
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        node_addr = 0x140000,
        table_addr = 0x150000,
        bitmask_slot = stack_top + 72,
        offset_slot = stack_top + 280,
        span_slot = stack_top + 296,
        bucket_bit = 26,
        callback_count = 28,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(node_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(table_addr), 1, P_READ | P_WRITE) < 0 ||
        pt_map_nothing(&mem, PAGE(callback_pc), 1, P_READ | P_WRITE) < 0) {
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
    cpu.sp = stack_top;
    cpu.x[19] = node_addr;

    if (a64_guest_write64(&cpu, &tlb, bitmask_slot, 1ULL << bucket_bit) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, offset_slot, table_addr - table_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, span_slot, 0xe0) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, node_addr, table_addr) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, callback_pc, 0xd65f03c0ULL) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    for (unsigned i = 0; i < callback_count; i++) {
        if (a64_guest_write64(&cpu, &tlb, table_addr + (unsigned long long)i * 8, callback_pc) !=
            A64_MEM_OK) {
            mem_destroy(&mem);
            return UINT64_MAX - 2;
        }
    }

    unsigned call_hits = 0;
    for (unsigned step = 0; step < 1024 && cpu.pc != restart_pc; step++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x7c080: insn = 0xf94027e0; break; // ldr x0, [sp, #72]
        case 0x7c084: insn = 0x36d7fbe0; break; // tbz w0, #26, 0x7c000
        case 0x7c088: insn = 0xf94097f7; break; // ldr x23, [sp, #296]
        case 0x7c08c: insn = 0xf9400278; break; // ldr x24, [x19]
        case 0x7c090: insn = 0xf9408fe0; break; // ldr x0, [sp, #280]
        case 0x7c094: insn = 0xd343fef7; break; // lsr x23, x23, #3
        case 0x7c098: insn = 0x8b000318; break; // add x24, x24, x0
        case 0x7c09c: insn = 0x14000004; break; // b 0x7c0ac
        case 0x7c0a0: insn = 0xd10006f7; break; // sub x23, x23, #1
        case 0x7c0a4: insn = 0xf8777b00; break; // ldr x0, [x24, x23, lsl #3]
        case 0x7c0a8: insn = 0xd63f0000; break; // blr x0
        case 0x7c0ac: insn = 0xb5ffffb7; break; // cbnz x23, 0x7c0a0
        case 0x7c0b0: insn = 0x17ffffd4; break; // b 0x7c000
        case callback_pc: insn = 0xd65f03c0; break; // ret
        default:
            mem_destroy(&mem);
            return 0x3000000000000000ULL | cpu.pc;
        }

        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) |
                   (cpu.pc & 0x0000ffffffffffffULL);
        }

        if (old_pc == loop_call) {
            call_hits++;
            cpu.x[0] = 1;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    if (cpu.pc != restart_pc) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | cpu.pc;
    }
    if (call_hits != callback_count) {
        mem_destroy(&mem);
        return 0x5000000000000000ULL | call_hits;
    }
    if (cpu.x[23] != 0) {
        mem_destroy(&mem);
        return 0x6000000000000000ULL | (cpu.x[23] & 0x0fffffffffffffffULL);
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_musl_memcpy8_tail_roundtrip(void)
{
    enum {
        src_addr = 0x160000,
        dst_addr = 0x161000,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(src_addr), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x182c8;
    cpu.x[0] = dst_addr;
    cpu.x[1] = src_addr;
    cpu.x[2] = 8;
    cpu.x[4] = src_addr + 8;
    cpu.x[5] = dst_addr + 8;

    if (a64_guest_write64(&cpu, &tlb, src_addr, 0x0123456789abcdefULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, dst_addr, 0xaaaaaaaaaaaaaaaaULL) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xf9400026, // ldr x6, [x1]
        0xf85f8087, // ldur x7, [x4, #-0x8]
        0xf9000006, // str x6, [x0]
        0xf81f80a7, // stur x7, [x5, #-0x8]
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t stored = UINT64_MAX;
    if (a64_guest_read64(&cpu, &tlb, dst_addr, &stored) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    result |= (stored == 0x0123456789abcdefULL) ? 0
                                                : (0x2000000000000000ULL |
                                                   (stored & 0x0fffffffffffffffULL));
    result |= (cpu.x[6] == 0x0123456789abcdefULL) ? 0
                                                  : (0x3000000000000000ULL |
                                                     (cpu.x[6] & 0x0fffffffffffffffULL));
    result |= (cpu.x[7] == 0x0123456789abcdefULL) ? 0
                                                  : (0x4000000000000000ULL |
                                                     (cpu.x[7] & 0x0fffffffffffffffULL));

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_mutex_unlock_normal_type_branches_to_fast_unlock(void)
{
    enum {
        mutex_addr = 0x240100,
        stack_addr = 0x250000,
        branch_target = 0x73808,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(mutex_addr), 1, P_READ | P_WRITE) < 0 ||
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
    cpu.sp = stack_addr + 0x800;
    cpu.x[0] = mutex_addr;
    cpu.x[29] = 0xabcdefULL;
    cpu.x[30] = 0x7b258ULL;

    if (a64_guest_write32(&cpu, cpu.tlb, mutex_addr + 0, 0) != A64_MEM_OK ||
        a64_guest_write32(&cpu, cpu.tlb, mutex_addr + 4, 0x10) != A64_MEM_OK ||
        a64_guest_write32(&cpu, cpu.tlb, mutex_addr + 8, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xa9bc7bfd, // stp x29, x30, [sp, #-0x40]!
        0xaa0003e2, // mov x2, x0
        0x910003fd, // mov x29, sp
        0xb9400808, // ldr w8, [x0, #0x8]
        0xb9400003, // ldr w3, [x0]
        0x2a2303e4, // mvn w4, w3
        0x72000c67, // ands w7, w3, #0xf
        0x12190084, // and w4, w4, #0x80
        0x54000820, // b.eq 0x73808
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, 0x736e4, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t result = 0;
    result |= (cpu.pc == branch_target) ? 0 : (0x2000000000000000ULL | cpu.pc);
    result |= ((uint32_t)cpu.x[7] == 0) ? 0 : 0x0100000000000000ULL;
    result |= ((uint32_t)cpu.x[4] == 0x80) ? 0 : 0x0200000000000000ULL;
    result |= ((cpu.pstate & 0x40000000ULL) != 0) ? 0 : 0x0400000000000000ULL;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_pthread_mutex_lock_fast_path(void)
{
    enum {
        mutex_addr = 0x230100,
        lock_word_addr = mutex_addr + 4,
        busy_value = 0x10,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(mutex_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = mutex_addr;

    if (a64_guest_write32(&cpu, cpu.tlb, mutex_addr, 0) != A64_MEM_OK ||
        a64_guest_write32(&cpu, cpu.tlb, lock_word_addr, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    cpu.pc = 0x6328c;
    for (int steps = 0; steps < 32 && cpu.pc >= 0x6328c && cpu.pc <= 0x632bc; steps++) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x6328c:
            insn = 0xb9400001; // ldr w1, [x0]
            break;
        case 0x63290:
            insn = 0xf2400c3f; // tst x1, #0xf
            break;
        case 0x63294:
            insn = 0x54000141; // b.ne 0x632bc
            break;
        case 0x63298:
            insn = 0x91001001; // add x1, x0, #0x4
            break;
        case 0x6329c:
            insn = 0x52800203; // mov w3, #0x10
            break;
        case 0x632a0:
            insn = 0x885ffc22; // ldaxr w2, [x1]
            break;
        case 0x632a4:
            insn = 0x350000a2; // cbnz w2, 0x632b8
            break;
        case 0x632a8:
            insn = 0x8802fc23; // stlxr w2, w3, [x1]
            break;
        case 0x632ac:
            insn = 0x35ffffa2; // cbnz w2, 0x632a0
            break;
        case 0x632b0:
            insn = 0x52800000; // mov w0, #0
            break;
        default:
            insn = 0;
            break;
        }
        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | cpu.pc;
        }
        if (cpu.pc == old_pc)
            cpu.pc += 4;
        if (cpu.pc == 0x632b4)
            break;
        if (cpu.pc == 0x632bc)
            break;
    }

    uint32_t lock_word = 0;
    if (a64_guest_read32(&cpu, cpu.tlb, lock_word_addr, &lock_word) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    result |= ((uint32_t)cpu.x[0] == 0) ? 0 : 1;
    result |= ((uint32_t)cpu.x[2] == 0) ? 0 : 2;
    result |= (lock_word == busy_value) ? 0 : 4;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_pthread_mutex_lock_prefix(void)
{
    enum {
        mutex_addr = 0x230100,
        lock_word_addr = mutex_addr + 4,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(mutex_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = mutex_addr;

    if (a64_guest_write32(&cpu, cpu.tlb, mutex_addr, 0) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    cpu.pc = 0x6328c;
    while (cpu.pc >= 0x6328c && cpu.pc <= 0x6329c) {
        uint32_t insn = 0;
        switch (cpu.pc) {
        case 0x6328c:
            insn = 0xb9400001; // ldr w1, [x0]
            break;
        case 0x63290:
            insn = 0xf2400c3f; // tst x1, #0xf
            break;
        case 0x63294:
            insn = 0x54000141; // b.ne 0x632bc
            break;
        case 0x63298:
            insn = 0x91001001; // add x1, x0, #0x4
            break;
        case 0x6329c:
            insn = 0x52800203; // mov w3, #0x10
            break;
        default:
            insn = 0;
            break;
        }
        uint64_t old_pc = cpu.pc;
        int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
        if (run_ret < 0) {
            mem_destroy(&mem);
            return 0x1000000000000000ULL | (((uint64_t)(uint8_t)(-run_ret)) << 48) | cpu.pc;
        }
        if (cpu.pc == 0x632bc)
            break;
        if (cpu.pc == old_pc)
            cpu.pc += 4;
    }

    uint64_t result = 0;
    result |= (cpu.pc == 0x632a0) ? 0 : 1;
    result |= (cpu.x[1] == lock_word_addr) ? 0 : 2;
    result |= ((uint32_t)cpu.x[3] == 0x10) ? 0 : 4;

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_musl_qsort_csinc_tst_gate_keeps_expected_path(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d3cc;
    cpu.x[22] = 0;
    cpu.x[26] = 2;

    static const uint32_t insns[] = {
        0x7100075f, // cmp w26, #1
        0x520002d6, // eor w22, w22, #1
        0x1a9fd7e0, // cset w0, gt  (csinc wzr, wzr, le)
        0x6a0002df, // tst w22, w0
        0x54000200, // b.eq 0x6d41c
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.pc != 0x6d3e0)
        return 0x2000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 1ULL)
        return 0x3000000000000000ULL | cpu.x[0];
    if (cpu.x[22] != 1ULL)
        return 0x4000000000000000ULL | cpu.x[22];

    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d3cc;
    cpu.x[22] = 0;
    cpu.x[26] = 1;
    run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0)
        return 0x5000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.pc != 0x6d41c)
        return 0x6000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 0ULL)
        return 0x7000000000000000ULL | cpu.x[0];
    if (cpu.x[22] != 1ULL)
        return 0x8000000000000000ULL | cpu.x[22];

    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_extract_prefix_preserves_pshift_state(void)
{
    enum {
        stack_top = 0x134000,
        stack_page = stack_top - PAGE_SIZE,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x6d644;
    cpu.sp = stack_top;
    cpu.x[0] = 0x40000a68ULL;
    cpu.x[19] = 0x40000a68ULL;
    cpu.x[21] = 0x6d82cULL;
    cpu.x[22] = 0x565e079cULL;
    cpu.x[25] = 0x1ffffffffffffff8ULL;
    cpu.x[26] = 0x5ULL;
    cpu.x[27] = 0xfffffef7f8f0ULL;
    cpu.x[28] = 0x40000a68ULL;

    static const uint32_t insns[] = {
        0x2a1c03e4, // mov w4, w28
        0x93d30b33, // extr x19, x25, x19, #2
        0xaa1b03e5, // mov x5, x27
        0xaa1603e3, // mov x3, x22
        0xaa1503e2, // mov x2, x21
        0xaa1a03e1, // mov x1, x26
        0xf90037e0, // str x0, [sp, #104]
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t spilled_head = 0;
    if (a64_guest_read64(&cpu, &tlb, stack_top + 104, &spilled_head) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    if (cpu.x[19] != 0x1000029aULL) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | cpu.x[19];
    }
    if (cpu.x[4] != 0x40000a68ULL) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | cpu.x[4];
    }
    if (cpu.x[1] != 0x5ULL || cpu.x[2] != 0x6d82cULL || cpu.x[3] != 0x565e079cULL ||
        cpu.x[5] != 0xfffffef7f8f0ULL) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | (cpu.x[1] & 0xffffffffULL);
    }
    if (spilled_head != 0x40000a68ULL) {
        mem_destroy(&mem);
        return 0x5000000000000000ULL | spilled_head;
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_rbit_clz_64bit_roundtrip(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d430;
    cpu.x[2] = 1ULL << 40;

    static const uint32_t insns[] = {
        0xdac00042, // rbit x2, x2
        0xdac01042, // clz x2, x2
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    if (cpu.x[2] != 40ULL)
        return 0x2000000000000000ULL | cpu.x[2];

    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_reentry_block_preserves_head_and_pshift_inputs(void)
{
    enum {
        stack_top = 0x132000,
        frame_base = stack_top - 0x400,
        stack_page = frame_base & ~((uint64_t)PAGE_SIZE - 1),
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x6d234;
    cpu.sp = stack_top;
    cpu.x[0] = 0x40000a68ULL;
    cpu.x[1] = 0x8ULL;
    cpu.x[2] = 0x6d82cULL;
    cpu.x[3] = 0x565e079cULL;
    cpu.x[4] = 0x3ULL;
    cpu.x[5] = 0xfffffef7f8f0ULL;
    cpu.x[19] = 0xaaaaaaaaaaaaaaaaULL;
    cpu.x[20] = 0xbbbbbbbbbbbbbbbbULL;
    cpu.x[21] = 0xccccccccccccccccULL;
    cpu.x[22] = 0xddddddddddddddddULL;
    cpu.x[23] = 0xeeeeeeeeeeeeeeeeULL;
    cpu.x[24] = 0xffffffffffffffffULL;
    cpu.x[25] = 0x1212121212121212ULL;
    cpu.x[26] = 0x3434343434343434ULL;
    cpu.x[27] = 0x5656565656565656ULL;
    cpu.x[28] = 0x7878787878787878ULL;
    cpu.x[29] = 0x9a9a9a9a9a9a9a9aULL;
    cpu.x[30] = 0xbcbcbcbcbcbcbcbcULL;

    static const uint32_t insns[] = {
        0xd11003ff, // sub sp, sp, #0x400
        0xa9007bfd, // stp x29, x30, [sp]
        0x910003fd, // mov x29, sp
        0xa90153f3, // stp x19, x20, [sp, #16]
        0x2a0403f3, // mov w19, w4
        0xa9025bf5, // stp x21, x22, [sp, #32]
        0xaa0203f5, // mov x21, x2
        0xaa0303f6, // mov x22, x3
        0xa90363f7, // stp x23, x24, [sp, #48]
        0xcb0103f8, // sub x24, xzr, x1
        0x52800037, // mov w23, #1
        0xa9046bf9, // stp x25, x26, [sp, #64]
        0xaa0003f9, // mov x25, x0
        0x910203fa, // add x26, sp, #0x80
        0xa90573fb, // stp x27, x28, [sp, #80]
        0xaa0503fb, // mov x27, x5
        0xaa0003fc, // mov x28, x0
        0xf90037e1, // str x1, [sp, #104]
        0xf9003fe0, // str x0, [sp, #120]
        0x14000009, // b 0x6d2a4
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    if (cpu.pc != 0x6d2a4ULL) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | cpu.pc;
    }
    if (cpu.sp != frame_base || cpu.x[29] != frame_base) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | cpu.sp;
    }
    if (cpu.x[19] != 0x3ULL || cpu.x[21] != 0x6d82cULL || cpu.x[22] != 0x565e079cULL ||
        cpu.x[23] != 0x1ULL || cpu.x[24] != 0xfffffffffffffff8ULL ||
        cpu.x[25] != 0x40000a68ULL || cpu.x[26] != frame_base + 0x80ULL ||
        cpu.x[27] != 0xfffffef7f8f0ULL || cpu.x[28] != 0x40000a68ULL) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | (cpu.x[26] & 0x0fffffffffffffffULL);
    }

    uint64_t saved_head = 0;
    uint64_t saved_rhs = 0;
    uint64_t saved_x29 = 0;
    uint64_t saved_x30 = 0;
    if (a64_guest_read64(&cpu, &tlb, frame_base + 120, &saved_head) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, frame_base + 104, &saved_rhs) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, frame_base + 0, &saved_x29) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, frame_base + 8, &saved_x30) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }
    if (saved_head != 0x40000a68ULL || saved_rhs != 0x8ULL || saved_x29 != 0x9a9a9a9a9a9a9a9aULL ||
        saved_x30 != 0xbcbcbcbcbcbcbcbcULL) {
        mem_destroy(&mem);
        return 0x5000000000000000ULL | (saved_head & 0x0fffffffffffffffULL);
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_restore_block_rebuilds_live_frame(void)
{
    enum {
        stack_top = 0x131000,
        stack_page = stack_top - PAGE_SIZE,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x6d4c8;
    cpu.sp = stack_top;
    cpu.x[19] = 0x1111111122222222ULL;
    cpu.x[23] = 0x3333333344444444ULL;
    cpu.x[24] = 0x5555555566666666ULL;
    cpu.x[26] = 0x1234567887654321ULL;
    cpu.x[27] = 0x7777777788888888ULL;
    cpu.x[28] = 0x99999999aaaaaaa0ULL;

    if (a64_guest_write64(&cpu, &tlb, stack_top + 0x00, 0x0101010101010101ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x08, 0x0202020202020202ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x10, 0x0202020202020202ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x18, 0x0303030303030303ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x20, 0x0303030303030303ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x28, 0x0404040404040404ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x30, 0x0505050505050505ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x38, 0x0606060606060606ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x40, 0x0707070707070707ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x48, 0x0808080808080808ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x50, 0x0909090909090909ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, stack_top + 0x58, 0x0a0a0a0a0a0a0a0aULL) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xa9407bfd, // ldp x29, x30, [sp]
        0xaa1b03e5, // mov x5, x27
        0xa9425bf5, // ldp x21, x22, [sp, #32]
        0x2a1a03e4, // mov w4, w26
        0xaa1803e3, // mov x3, x24
        0xaa1703e2, // mov x2, x23
        0xa9446bf9, // ldp x25, x26, [sp, #64]
        0xaa1303e1, // mov x1, x19
        0xa94363f7, // ldp x23, x24, [sp, #48]
        0xaa1c03e0, // mov x0, x28
        0xa94153f3, // ldp x19, x20, [sp, #16]
        0xa94573fb, // ldp x27, x28, [sp, #80]
        0x911043ff, // add sp, sp, #1040
        0x17ffff4e, // b 0x6d234
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    if (cpu.pc != 0x6d234ULL) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | cpu.pc;
    }
    if (cpu.sp != stack_top + 1040ULL) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | cpu.sp;
    }
    if (cpu.x[0] != 0x99999999aaaaaaa0ULL || cpu.x[1] != 0x1111111122222222ULL ||
        cpu.x[2] != 0x3333333344444444ULL || cpu.x[3] != 0x5555555566666666ULL ||
        cpu.x[4] != 0x87654321ULL || cpu.x[5] != 0x7777777788888888ULL) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | (cpu.x[4] & 0xffffffffULL);
    }
    if (cpu.x[19] != 0x0202020202020202ULL || cpu.x[20] != 0x0303030303030303ULL ||
        cpu.x[21] != 0x0303030303030303ULL || cpu.x[22] != 0x0404040404040404ULL ||
        cpu.x[23] != 0x0505050505050505ULL || cpu.x[24] != 0x0606060606060606ULL ||
        cpu.x[25] != 0x0707070707070707ULL || cpu.x[26] != 0x0808080808080808ULL ||
        cpu.x[27] != 0x0909090909090909ULL || cpu.x[28] != 0x0a0a0a0a0a0a0a0aULL ||
        cpu.x[29] != 0x0101010101010101ULL || cpu.x[30] != 0x0202020202020202ULL) {
        mem_destroy(&mem);
        return 0x5000000000000000ULL | (cpu.x[26] & 0x0fffffffffffffffULL);
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_shift_merge_block_uses_live_hot_regs(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d380;
    cpu.x[0] = 2ULL;
    cpu.x[2] = 62ULL;
    cpu.x[19] = 0x40000a90ULL;
    cpu.x[20] = 0x07ffffffffffffffULL;
    cpu.x[21] = 0xfffffef7f500ULL;
    cpu.x[25] = 0x1ffffffffffffff8ULL;

    static const uint32_t insns[] = {
        0x52800801, // mov w1, #0x40
        0x4b020022, // sub w2, w1, w2
        0x9ac02694, // lsr x20, x20, x0
        0x9ac22322, // lsl x2, x25, x2
        0xaa140054, // orr x20, x2, x20
        0x9ac02739, // lsr x25, x25, x0
        0x910022b5, // add x21, x21, #8
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    if (cpu.x[2] != 0x7fffffffffffffe0ULL)
        return 0x2000000000000000ULL | (cpu.x[2] & 0x0fffffffffffffffULL);
    if (cpu.x[20] != 0x7fffffffffffffffULL)
        return 0x3000000000000000ULL | (cpu.x[20] & 0x0fffffffffffffffULL);
    if (cpu.x[21] != 0xfffffef7f508ULL)
        return 0x4000000000000000ULL | (cpu.x[21] & 0x0fffffffffffffffULL);
    if (cpu.x[25] != 0x07fffffffffffffeULL)
        return 0x5000000000000000ULL | (cpu.x[25] & 0x0fffffffffffffffULL);

    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_tbnz_w0_signbit_branches(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d298;
    cpu.x[0] = 0x00000000fffffffaULL; // negative 32-bit comparator result

    static const uint32_t insn_6d298 = 0x37f80400; // tbnz w0, #31, 0x6d318
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn_6d298, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.pc != 0x6d318)
        return 0x2000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 0x00000000fffffffaULL)
        return 0x3000000000000000ULL | cpu.x[0];

    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d298;
    cpu.x[0] = 0x6aULL; // positive 32-bit comparator result
    run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn_6d298, 1);
    if (run_ret < 0)
        return 0x4000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.pc != 0x6d29c)
        return 0x5000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 0x6aULL)
        return 0x6000000000000000ULL | cpu.x[0];

    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d2d0;
    cpu.x[0] = 0x00000000fffffff0ULL; // negative 32-bit comparator result

    static const uint32_t insn_6d2d0 = 0x37fffda0; // tbnz w0, #31, 0x6d284
    run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn_6d2d0, 1);
    if (run_ret < 0)
        return 0x7000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.pc != 0x6d284)
        return 0x8000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 0x00000000fffffff0ULL)
        return 0x9000000000000000ULL | cpu.x[0];

    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d2d0;
    cpu.x[0] = 0x11ULL; // positive 32-bit comparator result
    run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn_6d2d0, 1);
    if (run_ret < 0)
        return 0xa000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    if (cpu.pc != 0x6d2d4)
        return 0xb000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 0x11ULL)
        return 0xc000000000000000ULL | cpu.x[0];

    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_tbz_w0_signbit_branch(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d3fc;
    cpu.x[0] = 0x00000000fffffff3ULL; // comparator returned a negative 32-bit result

    static const uint32_t insn = 0x36f805a0; // tbz w0, #31, 0x6d4b0
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    if (cpu.pc != 0x6d400)
        return 0x2000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 0x00000000fffffff3ULL)
        return 0x3000000000000000ULL | cpu.x[0];

    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x6d3fc;
    cpu.x[0] = 0x73ULL; // positive comparator result should take the branch

    run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x4000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    if (cpu.pc != 0x6d4b0)
        return 0x5000000000000000ULL | cpu.pc;
    if (cpu.x[0] != 0x73ULL)
        return 0x6000000000000000ULL | cpu.x[0];

    return 0;
}

uint64_t tcti_semantic_case_musl_qsort_trailing_zero_block(void)
{
    enum {
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        count_slot = stack_top + 100,
        stepson_slot = stack_top + 0,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x6d41c;
    cpu.sp = stack_top;
    cpu.x[20] = 0x07ffffffffffffffULL;
    cpu.x[21] = stepson_slot;
    cpu.x[28] = 0x40000a68ULL;

    if (a64_guest_write32(&cpu, &tlb, count_slot, 4U) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xb94067e0, // ldr w0, [sp, #100]
        0xd1000682, // sub x2, x20, #1
        0xf90002bc, // str x28, [x21]
        0x11000400, // add w0, w0, #1
        0xb90067e0, // str w0, [sp, #100]
        0xdac00042, // rbit x2, x2
        0xdac01042, // clz x2, x2
        0x2a0203e0, // mov w0, w2
        0x350000e2, // cbnz w2, ...
    };

    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, insns,
                                                   sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint32_t stored_count = 0;
    uint64_t stored_stepson = 0;
    if (a64_guest_read32(&cpu, &tlb, count_slot, &stored_count) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, stepson_slot, &stored_stepson) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    if (stored_count != 5U) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | stored_count;
    }
    if (stored_stepson != 0x40000a68ULL) {
        mem_destroy(&mem);
        return 0x3000000000000000ULL | stored_stepson;
    }
    if (cpu.x[2] != 1ULL) {
        mem_destroy(&mem);
        return 0x4000000000000000ULL | cpu.x[2];
    }
    if ((cpu.x[0] & 0xffffffffULL) != 1ULL) {
        mem_destroy(&mem);
        return 0x5000000000000000ULL | (cpu.x[0] & 0xffffffffULL);
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_qsort_pointer_slot_updates_roundtrip(void)
{
    enum {
        base_addr = 0x150000,
        slot0 = base_addr,
        slot1 = base_addr + 8,
        slot2 = base_addr + 16,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x5d1c8;
    cpu.x[0] = 16;                  // byte offset for str x1, [x23, x0]
    cpu.x[1] = 0x123456789abcdef0;  // value stored to slot2
    cpu.x[3] = slot0;               // source pointer for post-index update
    cpu.x[19] = slot0;              // base for ldp/ldr/str post-index
    cpu.x[20] = 8;                  // increment used by add x3, x3, x20
    cpu.x[23] = slot0;              // base for register-offset store

    if (a64_guest_write64(&cpu, &tlb, slot0, 0x1111111111111111ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, slot1, 0x2222222222222222ULL) != A64_MEM_OK ||
        a64_guest_write64(&cpu, &tlb, slot2, 0x3333333333333333ULL) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insns[] = {
        0xf8206ae1, // str x1, [x23, x0]
        0xa9400660, // ldp x0, x1, [x19]
        0xf9400263, // ldr x3, [x19]
        0x8b140063, // add x3, x3, x20
        0xf8008663, // str x3, [x19], #8
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t slot0_value = 0;
    uint64_t slot1_value = 0;
    uint64_t slot2_value = 0;
    if (a64_guest_read64(&cpu, &tlb, slot0, &slot0_value) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, slot1, &slot1_value) != A64_MEM_OK ||
        a64_guest_read64(&cpu, &tlb, slot2, &slot2_value) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    result |= (slot0_value == 0x1111111111111119ULL) ? 0
                                         : (0x2000000000000000ULL |
                                            (slot0_value & 0x0fffffffffffffffULL));
    result |= (slot1_value == 0x2222222222222222ULL) ? 0
                                                     : (0x3000000000000000ULL |
                                                        (slot1_value & 0x0fffffffffffffffULL));
    result |= (slot2_value == 0x123456789abcdef0ULL) ? 0
                                                     : (0x4000000000000000ULL |
                                                        (slot2_value & 0x0fffffffffffffffULL));
    result |= (cpu.x[0] == 0x1111111111111111ULL) ? 0
                                                  : (0x5000000000000000ULL |
                                                     (cpu.x[0] & 0x0fffffffffffffffULL));
    result |= (cpu.x[1] == 0x2222222222222222ULL) ? 0
                                                  : (0x6000000000000000ULL |
                                                     (cpu.x[1] & 0x0fffffffffffffffULL));
    result |= (cpu.x[3] == 0x1111111111111119ULL) ? 0
                                      : (0x7000000000000000ULL |
                                         (cpu.x[3] & 0x0fffffffffffffffULL));
    result |= (cpu.x[19] == slot1) ? 0
                                   : (0x8000000000000000ULL |
                                      (cpu.x[19] & 0x0fffffffffffffffULL));

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_reg_offset_ldr_w_helper_reads_hot_x0_offset(void)
{
    enum {
        base_addr = 0x015d38,
        load_offset = 0x5040,
        expected_word = 0x11223344U,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(base_addr), 8, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7a1cc;
    cpu.x[0] = load_offset;
    cpu.x[20] = base_addr;

    if (a64_guest_write32(&cpu, &tlb, base_addr + load_offset, expected_word) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    uint64_t meta = ((uint64_t)1 << 8) | ((uint64_t)A64_EXT_LSL << 24);
    int ret = _a64_tcti_ldr_x_helper(&cpu, cpu.pc, 7, 20, 0, A64_SIZE_W, A64_INDEX_OFFSET, meta);
    if (ret != 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint32_t)ret;
    }
    if (cpu.x[7] != expected_word) {
        mem_destroy(&mem);
        return 0x2000000000000000ULL | cpu.x[7];
    }

    mem_destroy(&mem);
    return 0;
}

uint64_t tcti_semantic_case_reg_offset_ldr_x0_alias_base_reads_expected_qword(void)
{
    enum {
        table_addr = 0xe0000,
        expected_qword = 0x17,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(table_addr), 1, P_READ | P_WRITE) < 0) {
        mem_destroy(&mem);
        return UINT64_MAX;
    }

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = 0x7bf54;
    cpu.x[0] = table_addr;
    cpu.x[25] = 8;

    if (a64_guest_write64(&cpu, &tlb, table_addr + 8, expected_qword) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    uint64_t meta = ((uint64_t)1 << 8) | ((uint64_t)25 << 16) | ((uint64_t)A64_EXT_LSL << 24) |
                    ((uint64_t)1 << 40);
    int ret = _a64_tcti_ldr_x_helper(&cpu, cpu.pc, 0, 0, 0, A64_SIZE_X, A64_INDEX_OFFSET, meta);
    if (ret != 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint32_t)ret;
    }

    uint64_t result = cpu.x[0];
    mem_destroy(&mem);
    return result == expected_qword ? 0ULL : (0x2000000000000000ULL | result);
}

uint64_t tcti_semantic_case_sp_relative_ldr_str_roundtrip(void)
{
    enum {
        stack_top = 0x130000,
        stack_page = stack_top - PAGE_SIZE,
        expected = 1ULL << 35,
    };

    struct mem mem;
    mem_init(&mem);
    if (pt_map_nothing(&mem, PAGE(stack_page), 2, P_READ | P_WRITE) < 0) {
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
    cpu.x[2] = expected;

    static const uint32_t insns[] = {
        0xf90027e2, // str x2, [sp, #72]
        0xf94027e3, // ldr x3, [sp, #72]
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, 0x7c070, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t stored = 0;
    if (a64_guest_read64(&cpu, &tlb, stack_top + 72, &stored) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    uint64_t result = 0;
    result |= (stored == expected) ? 0 : (0x2000000000000000ULL | (stored & 0x0fffffffffffffffULL));
    result |= (cpu.x[3] == expected) ? 0 : (0x3000000000000000ULL | (cpu.x[3] & 0x0fffffffffffffffULL));
    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_str_xzr_post_index_writes_back_base(void)
{
    enum {
        guest_addr = 0x150000,
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
    cpu.pc = 0x7c034;
    cpu.x[0] = guest_addr;

    if (a64_guest_write64(&cpu, &tlb, guest_addr, 0xaaaaaaaaaaaaaaaaULL) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 1;
    }

    static const uint32_t insn = 0xf800841f; // str xzr, [x0], #8
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0) {
        mem_destroy(&mem);
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);
    }

    uint64_t stored = UINT64_MAX;
    if (a64_guest_read64(&cpu, &tlb, guest_addr, &stored) != A64_MEM_OK) {
        mem_destroy(&mem);
        return UINT64_MAX - 2;
    }

    uint64_t result = 0;
    result |= (stored == 0) ? 0 : (0x2000000000000000ULL | (stored & 0x0fffffffffffffffULL));
    result |= (cpu.x[0] == guest_addr + 8) ? 0
                                           : (0x3000000000000000ULL |
                                              (cpu.x[0] & 0x0fffffffffffffffULL));

    mem_destroy(&mem);
    return result;
}

uint64_t tcti_semantic_case_udiv_preserves_flags_for_csel_eq(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x7989c;
    cpu.x[1] = 8;
    cpu.x[5] = 0x1111111111111111ULL;
    cpu.x[6] = 0x2222222222222222ULL;
    cpu.x[7] = 2;
    cpu.x[9] = 0;

    static const uint32_t insns[] = {
        0xeb09013f, // cmp x9, x9
        0x1ac70823, // udiv w3, w1, w7
        0x9a8600a4, // csel x4, x5, x6, eq
    };

    int run_ret =
        a64_cpu_execute_code_block(&cpu, cpu.pc, insns, sizeof(insns) / sizeof(insns[0]));
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    if (cpu.x[3] != 4)
        return 0x2000000000000000ULL | cpu.x[3];
    return cpu.x[4];
}

uint64_t tcti_semantic_case_umaddl_uses_unsigned_32bit_inputs(void)
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x7992c;
    cpu.x[0] = 0xffffffff00000005ULL;
    cpu.x[11] = 0xffffffff00000007ULL;

    static const uint32_t insn = 0x9bab7c01; // umaddl x1, w0, w11, xzr
    int run_ret = a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1);
    if (run_ret < 0)
        return 0x1000000000000000ULL | (uint64_t)(uint8_t)(-run_ret);

    return cpu.x[1];
}
