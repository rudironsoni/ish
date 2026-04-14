/*
 * aarch64 Instruction Fetch - Stage [1] FETCH contract
 *
 * Contract: Fetch 32-bit A64 instruction word from guest memory
 * Boundary: B1 (guest PC + TLB state) -> B2 (instruction word OR fault)
 *
 * Deterministic requirements:
 * 1. Same PC + valid mapping → same instruction word
 * 2. Invalid PC → deterministic -EFAULT fault
 * 3. Cross-page → deterministic behavior (word or fault)
 */

#ifndef A64_FETCH_H
#define A64_FETCH_H

#include <stdint.h>

struct cpu_state;
struct tlb;

/*
 * Fetch an instruction from guest memory using TLB
 *
 * B1 Input:
 *   - pc: Guest PC (64-bit virtual address)
 *   - tlb: Translation lookaside buffer with current mappings
 *
 * B2 Output:
 *   - *insn: 32-bit A64 instruction word (filled on success)
 *   - Return: 0 on success, -EFAULT on fault
 *
 * Owner: Sources/IXLandLinuxRuntime/emu/aarch64/fetch.c
 */
int a64_fetch_insn(struct cpu_state *cpu, struct tlb *tlb, uint64_t pc, uint32_t *insn);

#endif /* A64_FETCH_H */
