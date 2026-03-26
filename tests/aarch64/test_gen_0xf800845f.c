/*
 * Generator unit test for decoded instruction str xzr, [x2], #8
 * Verifies emitted bytecode matches decoded fields
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"
#include "tcti/aarch64/gen.h"

int main(void) {
    a64_instr_t instr;
    a64_gen_state_t gen_state;
    tcti_gadget_t gadget_buffer[32];
    int ret;
    uint64_t test_pc = 0xf7fa4650ULL;
    
    printf("Testing generator emission for str xzr, [x2], #8 at PC 0x%llx\n\n", (unsigned long long)test_pc);
    
    // Decode first
    uint32_t raw_insn = 0xf800845f;
    memset(&instr, 0, sizeof(instr));
    ret = a64_decode(raw_insn, &instr);
    if (ret != 0) {
        printf("FAIL: Decoder returned error %d\n", ret);
        return 1;
    }
    
    // Set PC in instruction
    // Note: generator expects PC in state, not in instr
    
    // Initialize generator state
    memset(&gen_state, 0, sizeof(gen_state));
    gen_state.guest_pc = test_pc;
    
    ret = a64_gen_init(&gen_state, gadget_buffer, 32);
    if (ret != A64_GEN_OK) {
        printf("FAIL: a64_gen_init returned %d\n", ret);
        return 1;
    }
    
    // Generate load/store
    ret = a64_gen_ldst(&gen_state, &instr);
    if (ret != A64_GEN_OK) {
        printf("FAIL: a64_gen_ldst returned %d\n", ret);
        return 1;
    }
    
    printf("Generated %zu gadgets\n\n", gen_state.num_gadgets);
    
    // Check emission by inspecting the bytecode
    // The bytecode is at gen_state.bytecode[0] to [gen_state.bytecode_pos]
    // Format: gadget, fault_pc, Rt, Rn, imm, size, idx_mode, meta
    
    printf("Bytecode dump:\n");
    for (size_t i = 0; i < gen_state.bytecode_pos / sizeof(uint64_t); i++) {
        uint64_t val = ((uint64_t*)gen_state.bytecode)[i];
        printf("  [%zu]: 0x%016llx\n", i, (unsigned long long)val);
    }
    
    printf("\n");
    
    // Parse bytecode
    uint64_t *bc = (uint64_t*)gen_state.bytecode;
    int idx = 0;
    
    // First entry: gadget pointer
    tcti_gadget_t emitted_gadget = (tcti_gadget_t)(uintptr_t)bc[idx++];
    uint64_t emitted_fault_pc = bc[idx++];
    uint64_t emitted_rt = bc[idx++];
    uint64_t emitted_rn = bc[idx++];
    uint64_t emitted_imm = bc[idx++];
    uint64_t emitted_size = bc[idx++];
    uint64_t emitted_idx_mode = bc[idx++];
    uint64_t emitted_meta = bc[idx++];
    
    printf("Emitted values:\n");
    printf("  gadget:     %p (expected: gadget_str_x)\n", (void*)emitted_gadget);
    printf("  fault_pc:   0x%016llx (expected: 0x%016llx)\n", 
           (unsigned long long)emitted_fault_pc, (unsigned long long)test_pc);
    printf("  Rt:         %llu (expected: 31)\n", (unsigned long long)emitted_rt);
    printf("  Rn:         %llu (expected: 2)\n", (unsigned long long)emitted_rn);
    printf("  imm:        %lld (expected: 8)\n", (long long)emitted_imm);
    printf("  size:       %llu (expected: 3)\n", (unsigned long long)emitted_size);
    printf("  idx_mode:   %llu (expected: %d = POST_INDEX)\n", 
           (unsigned long long)emitted_idx_mode, A64_POST_INDEX);
    printf("  meta:       0x%016llx (expected: 0)\n", (unsigned long long)emitted_meta);
    
    printf("\n");
    
    // Assertions
    int failed = 0;
    
    if (emitted_fault_pc != test_pc) {
        printf("FAIL: fault_pc != test_pc\n");
        failed++;
    }
    
    if (emitted_rt != 31) {
        printf("FAIL: Rt != 31 (got %llu)\n", (unsigned long long)emitted_rt);
        failed++;
    }
    
    if (emitted_rn != 2) {
        printf("FAIL: Rn != 2 (got %llu)\n", (unsigned long long)emitted_rn);
        failed++;
    }
    
    if (emitted_imm != 8) {
        printf("FAIL: imm != 8 (got %lld)\n", (long long)emitted_imm);
        failed++;
    }
    
    if (emitted_size != 3) {
        printf("FAIL: size != 3 (got %llu)\n", (unsigned long long)emitted_size);
        failed++;
    }
    
    if (emitted_idx_mode != A64_POST_INDEX) {
        printf("FAIL: idx_mode != A64_POST_INDEX (got %llu, expected %d)\n", 
               (unsigned long long)emitted_idx_mode, A64_POST_INDEX);
        failed++;
    }
    
    if (emitted_meta != 0) {
        printf("FAIL: meta != 0 (got 0x%016llx)\n", (unsigned long long)emitted_meta);
        failed++;
    }
    
    if (failed == 0) {
        printf("PASS: All assertions passed\n");
        return 0;
    } else {
        printf("FAIL: %d assertions failed\n", failed);
        return 1;
    }
}
