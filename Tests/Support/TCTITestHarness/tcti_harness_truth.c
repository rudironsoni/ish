#include "tcti_harness_truth.h"

#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#include <string.h>

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
