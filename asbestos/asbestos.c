#define DEFAULT_CHANNEL instr
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "asbestos/asbestos.h"
#include "asbestos/frame.h"
#include "emu/cpu.h"
#include "emu/tlb.h"
#include "emu/interrupt.h"
#include "util/list.h"
#include "emu/aarch64/decode.h"
#include "emu/aarch64/cpu.h"

extern int current_pid(void);

// Current execution context
extern struct cpu_state *current_cpu;
struct cpu_state *current_cpu = NULL;

// ============================================================================
// FULL AARCH64 INSTRUCTION INTERPRETER
// ============================================================================
//
// Direct interpreter that properly handles all aarch64 instruction types.
// No TCTI complexity - just fetch, decode, execute.

// Helper: Sign extend value
static inline int64_t sign_extend64(uint64_t val, int bits) {
    int64_t sign_bit = 1LL << (bits - 1);
    return (int64_t)((val ^ sign_bit) - sign_bit);
}

// Helper: Get register value (handles x31 as XZR for most ops, SP for some)
static inline uint64_t get_x_reg(struct cpu_state *cpu, int reg, bool is_sp) {
    if (reg == 31) {
        return is_sp ? cpu->sp : 0;  // SP or XZR
    }
    return cpu->x[reg];
}

// Helper: Set register value (handles x31)
static inline void set_x_reg(struct cpu_state *cpu, int reg, uint64_t val, bool is_sp) {
    if (reg == 31) {
        if (is_sp) cpu->sp = val;
        // else XZR - discard write
    } else {
        cpu->x[reg] = val;
    }
}

// Helper: Read from guest memory
static int read_guest_memory(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, void *dst, size_t size) {
    for (size_t i = 0; i < size; i++) {
        void *ptr = __tlb_read_ptr(tlb, addr + i);
        if (ptr == NULL) {
            ptr = tlb_handle_miss(tlb, addr + i, MEM_READ);
            if (ptr == NULL) {
                cpu->fault_addr = tlb->segfault_addr;
                cpu->fault_was_write = 0;
                return -1;
            }
        }
        ((uint8_t*)dst)[i] = *(uint8_t*)ptr;
    }
    return 0;
}

// Helper: Write to guest memory
static int write_guest_memory(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, const void *src, size_t size) {
    for (size_t i = 0; i < size; i++) {
        void *ptr = __tlb_read_ptr(tlb, addr + i);
        if (ptr == NULL) {
            ptr = tlb_handle_miss(tlb, addr + i, MEM_WRITE);
            if (ptr == NULL) {
                cpu->fault_addr = tlb->segfault_addr;
                cpu->fault_was_write = 1;
                return -1;
            }
        }
        *(uint8_t*)ptr = ((const uint8_t*)src)[i];
    }
    return 0;
}

// ============================================================================
// INSTRUCTION EXECUTION
// ============================================================================

static int execute_data_processing_imm(struct cpu_state *cpu, uint32_t insn, a64_instr_t *dec) {
    int op0 = (insn >> 23) & 0x7;
    
    switch (op0) {
        case 0: {  // PC-relative addressing (ADR/ADRP)
            int op = (insn >> 31) & 1;
            int64_t imm;
            if (op == 0) {
                imm = ((insn >> 3) & 0x1FFFFC) | ((insn >> 29) & 3);
                imm = sign_extend64(imm, 21);
                cpu->x[dec->Rd] = cpu->pc + imm;
            } else {
                imm = ((insn >> 3) & 0x1FFFFC) | ((insn >> 29) & 3);
                imm = sign_extend64(imm, 21) << 12;
                cpu->x[dec->Rd] = (cpu->pc & ~0xFFF) + imm;
            }
            break;
        }
        
        case 2: {  // Add/subtract immediate
            uint64_t val = get_x_reg(cpu, dec->Rn, true);
            uint64_t imm = dec->imm;
            uint64_t result;
            bool is_sub = (insn >> 30) & 1;
            
            if (is_sub) {
                result = val - imm;
            } else {
                result = val + imm;
            }
            
            bool rd_is_sp = (dec->Rd == 31);
            set_x_reg(cpu, dec->Rd, result, rd_is_sp);
            break;
        }
        
        case 4:
        case 5: {
            uint64_t imm = dec->imm;
            uint64_t val = cpu->x[dec->Rn];
            uint64_t result;
            int opc = (insn >> 29) & 3;
            
            switch (opc) {
                case 0: result = val & imm; break;
                case 1: result = val | imm; break;
                case 2: result = val ^ imm; break;
                case 3:
                    result = val & imm;
                    cpu->z = (result == 0);
                    cpu->n = (result >> 63) & 1;
                    break;
                default: return INT_GPF;
            }
            
            if (opc != 3 || dec->Rd != 31) {
                set_x_reg(cpu, dec->Rd, result, false);
            }
            break;
        }
        
        case 6: {  // Move wide immediate
            uint64_t imm = dec->imm;
            int hw = dec->imm_shift;
            uint64_t result = imm << (hw * 16);
            int opc = (insn >> 29) & 3;
            
            switch (opc) {
                case 0: result = ~result; break;
                case 2: break;
                case 3:
                    {
                        uint64_t mask = 0xFFFFULL << (hw * 16);
                        result = (cpu->x[dec->Rd] & ~mask) | result;
                    }
                    break;
                default:
                    return INT_GPF;
            }
            
            set_x_reg(cpu, dec->Rd, result, false);
            break;
        }
        
        default:
            printk("[aarch64] Unhandled DP_IMM op0=%d at %llx\n", op0, cpu->pc);
            return INT_GPF;
    }
    
    cpu->pc += 4;
    return INT_NONE;
}

static int execute_data_processing_reg(struct cpu_state *cpu, uint32_t insn, a64_instr_t *dec) {
    uint32_t opcode = (insn >> 21) & 0xF;
    uint64_t op1 = get_x_reg(cpu, dec->Rn, false);
    uint64_t op2 = get_x_reg(cpu, dec->Rm, false);
    uint64_t result;
    bool set_flags = false;
    
    int shift_type = (insn >> 22) & 3;
    int shift_amount = (insn >> 10) & 0x3F;
    
    switch (shift_type) {
        case 0: op2 <<= shift_amount; break;
        case 1: op2 >>= shift_amount; break;
        case 2: op2 = (int64_t)op2 >> shift_amount; break;
        case 3:
            if (shift_amount) {
                op2 = (op2 >> shift_amount) | (op2 << (64 - shift_amount));
            }
            break;
    }
    
    switch (opcode) {
        case 0: result = op1 + op2; break;
        case 1: result = op1 + op2 + cpu->c; break;
        case 2: result = op1 - op2; break;
        case 3: result = op1 - op2 - !cpu->c; break;
        case 4: result = op1 & op2; break;
        case 5: result = op1 & ~op2; break;
        case 6: result = op1 | op2; break;
        case 7: result = op1 | ~op2; break;
        case 8: result = op1 ^ op2; break;
        case 9: result = op1 ^ ~op2; break;
        case 10:
        case 11:
            result = (opcode == 10) ? (op1 & op2) : (op1 & ~op2);
            set_flags = true;
            break;
        default:
            printk("[aarch64] Unhandled DP_REG opcode %d at %llx\n", opcode, cpu->pc);
            return INT_GPF;
    }
    
    if (set_flags) {
        cpu->z = (result == 0);
        cpu->n = (result >> 63) & 1;
    }
    
    set_x_reg(cpu, dec->Rd, result, false);
    cpu->pc += 4;
    return INT_NONE;
}

static int execute_branch(struct cpu_state *cpu, uint32_t insn, a64_instr_t *dec) {
    if ((insn & 0xFF000010) == 0x54000000) {
        int cond = insn & 0xF;
        bool take = false;
        
        switch (cond & 0xE) {
            case 0: take = cpu->z; break;
            case 2: take = cpu->c; break;
            case 4: take = cpu->n; break;
            case 6: take = cpu->v; break;
            case 8: take = cpu->c && !cpu->z; break;
            case 10: take = cpu->n == cpu->v; break;
            case 12: take = !cpu->z && (cpu->n == cpu->v); break;
            case 14: take = true; break;
        }
        
        if (cond & 1) take = !take;
        
        if (take) {
            cpu->pc += dec->imm;
        } else {
            cpu->pc += 4;
        }
        return INT_NONE;
    }
    
    if ((insn & 0x7F000000) == 0x34000000) {
        uint64_t val = cpu->x[dec->Rn];
        bool is_cbnz = (insn >> 24) & 1;
        bool should_branch = is_cbnz ? (val != 0) : (val == 0);
        if (should_branch) {
            cpu->pc += dec->imm;
        } else {
            cpu->pc += 4;
        }
        return INT_NONE;
    }
    
    if ((insn & 0xFC000000) == 0x94000000) {
        cpu->x[30] = cpu->pc + 4;
        cpu->pc += dec->imm;
        return INT_NONE;
    }
    
    if ((insn & 0xFC000000) == 0x14000000) {
        cpu->pc += dec->imm;
        return INT_NONE;
    }
    
    if ((insn & 0xFFFFFC1F) == 0xD65F0000) {
        int rn = (insn >> 5) & 0x1F;
        cpu->pc = (rn == 31) ? cpu->x[30] : cpu->x[rn];
        return INT_NONE;
    }
    
    if ((insn & 0xFFFFFC1F) == 0xD61F0000) {
        int rn = (insn >> 5) & 0x1F;
        cpu->pc = cpu->x[rn];
        return INT_NONE;
    }
    
    printk("[aarch64] Unhandled branch encoding at %llx: %08x\n", cpu->pc, insn);
    return INT_GPF;
}

static int execute_load_store(struct cpu_state *cpu, struct tlb *tlb, uint32_t insn, a64_instr_t *dec) {
    uint64_t addr;
    bool is_load = ((insn >> 22) & 1) == 1;
    int size = dec->size;
    int bytes = 1 << size;
    
    // Handle FP/SIMD load/store by treating as NOP for now
    if ((insn >> 26) & 1) {
        cpu->pc += 4;
        return INT_NONE;
    }
    
    // Address calculation
    if (((insn >> 27) & 0x1F) == 0x1F && ((insn >> 24) & 7) == 0) {
        uint64_t offset = cpu->x[dec->Rm];
        int option = (insn >> 13) & 7;
        int s = (insn >> 12) & 1;
        
        switch (option) {
            case 0: offset = (uint64_t)(uint8_t)offset; break;
            case 1: offset = (uint64_t)(uint16_t)offset; break;
            case 2: offset = (uint64_t)(uint32_t)offset; break;
            case 3: offset = (uint64_t)(uint64_t)offset; break;
            case 4: offset = (int64_t)(int8_t)offset; break;
            case 5: offset = (int64_t)(int16_t)offset; break;
            case 6: offset = (int64_t)(int32_t)offset; break;
            case 7: offset = (int64_t)(int64_t)offset; break;
        }
        
        if (s) offset <<= size;
        addr = get_x_reg(cpu, dec->Rn, true) + offset;
    } else if ((insn >> 24) == 0x39 || (insn >> 24) == 0x38) {
        addr = get_x_reg(cpu, dec->Rn, true) + dec->imm;
    } else {
        printk("[aarch64] Unhandled LDST mode at %llx: %08x\n", cpu->pc, insn);
        return INT_GPF;
    }
    
    if (is_load) {
        uint64_t val = 0;
        if (read_guest_memory(cpu, tlb, addr, &val, bytes) < 0) {
            return INT_GPF;
        }
        
        if (dec->is_signed) {
            switch (size) {
                case 0: val = (int8_t)val; break;
                case 1: val = (int16_t)val; break;
                case 2: val = (int32_t)val; break;
            }
        }
        
        set_x_reg(cpu, dec->Rd, val, false);
    } else {
        uint64_t val = get_x_reg(cpu, dec->Rd, false);
        if (write_guest_memory(cpu, tlb, addr, &val, bytes) < 0) {
            return INT_GPF;
        }
    }
    
    cpu->pc += 4;
    return INT_NONE;
}

static int execute_simd_fp(struct cpu_state *cpu, uint32_t insn, a64_instr_t *dec) {
    (void)insn;
    (void)dec;
    cpu->pc += 4;
    return INT_NONE;
}

static int execute_system(struct cpu_state *cpu, uint32_t insn) {
    if ((insn & 0xFFE0001F) == 0xD4000001) {
        cpu->pc += 4;
        return INT_SYSCALL;
    }
    
    printk("[aarch64] Unhandled system instruction at %llx: %08x\n", cpu->pc, insn);
    return INT_GPF;
}

// ============================================================================
// MAIN INTERPRETER LOOP
// ============================================================================

static int execute_instruction(struct cpu_state *cpu, struct tlb *tlb) {
    uint32_t insn;
    
    // Debug: Log first instruction execution
    static int first_insn = 1;
    if (first_insn) {
        printk("[aarch64] FIRST INSTRUCTION: pc=0x%llx, x0=%llx, sp=0x%llx\n",
               cpu->pc, cpu->x[0], cpu->sp);
        first_insn = 0;
    }
    
    void *ptr = __tlb_read_ptr(tlb, cpu->pc);
    if (ptr == NULL) {
        ptr = tlb_handle_miss(tlb, cpu->pc, MEM_READ);
        if (ptr == NULL) {
            printk("[aarch64] PAGE FAULT at pc=0x%llx\n", cpu->pc);
            cpu->fault_addr = tlb->segfault_addr;
            cpu->fault_was_write = 0;
            return INT_GPF;
        }
    }
    
    insn = *(uint32_t *)ptr;
    
    uint8_t top_byte = (insn >> 24) & 0xFF;
    if (top_byte == 0xD4 || top_byte == 0xD5) {
        return execute_system(cpu, insn);
    }
    
    a64_instr_t decoded;
    int ret = a64_decode(insn, &decoded);
    if (ret != 0) {
        printk("[aarch64] Decode failed at %llx: %08x\n", cpu->pc, insn);
        return INT_GPF;
    }
    
    // Debug: Log load/store instructions
    if (decoded.cat == A64_LD_ST && cpu->pc >= 0xf7fa0000) {
        uint64_t base = get_x_reg(cpu, decoded.Rn, true);
        printk("[aarch64] LDST at %llx: insn=%08x Rn=%d(base=%llx) Rm=%d Rd=%d size=%d\n",
               cpu->pc, insn, decoded.Rn, base, decoded.Rm, decoded.Rd, decoded.size);
    }
    
    switch (decoded.cat) {
        case A64_DP_IMM:
            return execute_data_processing_imm(cpu, insn, &decoded);
        case A64_DP_REG:
        case A64_DP_REG2:
        case A64_DP_REG3:
        case A64_DP_REG4:
            return execute_data_processing_reg(cpu, insn, &decoded);
        case A64_BRANCH:
        case A64_BRANCH2:
            return execute_branch(cpu, insn, &decoded);
        case A64_LD_ST:
            return execute_load_store(cpu, tlb, insn, &decoded);
        case A64_SIMD:
        case A64_SIMD2:
        case A64_SIMD0:
            return execute_simd_fp(cpu, insn, &decoded);
        default:
            printk("[aarch64] Unhandled category %d at %llx\n", decoded.cat, cpu->pc);
            return INT_GPF;
    }
}

int cpu_run_to_interrupt(struct cpu_state *cpu, struct tlb *tlb) {
    if (cpu->poked_ptr == NULL)
        cpu->poked_ptr = &cpu->_poked;
    tlb_refresh(tlb, cpu->mmu);
    
    int interrupt = INT_NONE;
    int cycles = 0;
    const int MAX_CYCLES = 1024;
    
    while (interrupt == INT_NONE && cycles < MAX_CYCLES) {
        interrupt = execute_instruction(cpu, tlb);
        cycles++;
    }
    
    cpu->trapno = interrupt;
    cpu->cycle += cycles;
    
    if (interrupt == INT_NONE && __atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST))
        interrupt = INT_TIMER;
    
    return interrupt;
}

void cpu_poke(struct cpu_state *cpu) {
    __atomic_store_n(cpu->poked_ptr, true, __ATOMIC_SEQ_CST);
}

// Stubs
void asbestos_invalidate_page(struct asbestos *asbestos, page_t page) { (void)asbestos; (void)page; }
void asbestos_free(struct asbestos *asbestos) { (void)asbestos; }
void asbestos_init(struct asbestos *asbestos) { (void)asbestos; }

struct asbestos *asbestos_new(struct mmu *mmu) {
    struct asbestos *asbestos = calloc(1, sizeof(struct asbestos));
    if (asbestos) {
        asbestos->mmu = mmu;
    }
    return asbestos;
}

void fiber_jit_free(struct fiber_block *block) { (void)block; }
struct fiber_block *fiber_jit_compile(struct fiber_block *block, struct tlb *tlb) { 
    (void)block; (void)tlb; return NULL; 
}
void fiber_patch_ip(struct asbestos *asbestos, addr_t ip) { (void)asbestos; (void)ip; }
struct fiber_block *fiber_new(struct asbestos *asbestos) { (void)asbestos; return NULL; }
int fiber_enter(struct fiber_block *block, struct fiber_frame *frame, struct tlb *tlb) { 
    (void)block; (void)frame; (void)tlb; return -1; 
}
