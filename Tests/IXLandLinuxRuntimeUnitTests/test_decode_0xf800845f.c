/*
 * Decoder unit test for raw instruction 0xf800845f
 * Expected: str xzr, [x2], #8 (POST_INDEX)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>

int main(void) {
    a64_instr_t instr;
    int ret;
    
    // Raw instruction: str xzr, [x2], #8
    uint32_t raw_insn = 0xf800845f;
    
    printf("Testing decode of 0x%08x (str xzr, [x2], #8)\n\n", raw_insn);
    
    memset(&instr, 0, sizeof(instr));
    ret = a64_decode(raw_insn, &instr);
    
    if (ret != 0) {
        printf("FAIL: Decoder returned error %d\n", ret);
        return 1;
    }
    
    printf("Decoded fields:\n");
    printf("  cat:        %d (expected: %d = A64_LD_ST)\n", instr.cat, A64_LD_ST);
    printf("  subtype:    %d (expected: %d = A64_LDST_SINGLE)\n", instr.subtype, A64_LDST_SINGLE);
    printf("  Rd:         %d (expected: 31)\n", instr.Rd);
    printf("  Rn:         %d (expected: 2)\n", instr.Rn);
    printf("  imm:        %ld (expected: 8)\n", (long)instr.imm);
    printf("  idx_mode:   %d (expected: %d = POST_INDEX)\n", instr.idx_mode, A64_POST_INDEX);
    printf("  size:       %d (expected: %d = 64-bit)\n", instr.size, A64_SIZE_X);
    printf("  is_64bit:   %d (expected: 1)\n", instr.is_64bit);
    printf("  is_vector:  %d (expected: 0)\n", instr.is_vector);
    
    printf("\n");
    
    // Assertions
    int failed = 0;
    
    if (instr.cat != A64_LD_ST) {
        printf("FAIL: cat != A64_LD_ST\n");
        failed++;
    }
    
    if (instr.subtype != A64_LDST_SINGLE) {
        printf("FAIL: subtype != A64_LDST_SINGLE\n");
        failed++;
    }
    
    if (instr.Rd != 31) {
        printf("FAIL: Rd != 31 (got %d)\n", instr.Rd);
        failed++;
    }
    
    if (instr.Rn != 2) {
        printf("FAIL: Rn != 2 (got %d)\n", instr.Rn);
        failed++;
    }
    
    if (instr.imm != 8) {
        printf("FAIL: imm != 8 (got %ld)\n", (long)instr.imm);
        failed++;
    }
    
    if (instr.idx_mode != A64_POST_INDEX) {
        printf("FAIL: idx_mode != A64_POST_INDEX (got %d, expected %d)\n", 
               instr.idx_mode, A64_POST_INDEX);
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
