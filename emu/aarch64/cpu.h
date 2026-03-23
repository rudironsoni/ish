#ifndef EMU_AARCH64_CPU_H
#define EMU_AARCH64_CPU_H

#include "misc.h"
#include "emu/mmu.h"
#include "emu/aarch64/decode.h"

#ifdef __KERNEL__
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

struct cpu_state;
struct tlb;
int cpu_run_to_interrupt(struct cpu_state *cpu, struct tlb *tlb);
void cpu_poke(struct cpu_state *cpu);

// TCTI execution functions
void a64_cpu_init(struct cpu_state *cpu);
void a64_cpu_run(struct cpu_state *cpu, struct tlb *tlb);
int a64_cpu_step(struct cpu_state *cpu, struct tlb *tlb);
void a64_cpu_dump(struct cpu_state *cpu);
int a64_execute_ldst(struct cpu_state *cpu, struct tlb *tlb, const a64_instr_t *instr);
int a64_execute_bitfield(struct cpu_state *cpu, const a64_instr_t *instr);

// aarch64 has 31 general-purpose registers (x0-x30)
// x30 is the link register (lr)
// Stack pointer is separate from x31 (wzr/xzr)
// Program counter is not directly accessible as a GPR

// SIMD/FP register: 128-bit vector register
// Can be accessed as:
//   qN: 128-bit (__int128)
//   dN: 64-bit (double)
//   sN: 32-bit (float)
//   hN: 16-bit (half)
//   bN: 8-bit (byte)
union vec_reg {
    __int128 q;
    qword_t d[2];
    dword_t s[4];
    word_t h[8];
    byte_t b[16];
    float f32[4];
    double f64[2];
};
static_assert(sizeof(union vec_reg) == 16, "vec_reg size");

// aarch64 CPU state
struct cpu_state {
    struct mmu *mmu;
    long cycle;

    // 31 general-purpose registers (x0-x30)
    // Note: x31 is not a real register (it's either SP or XZR depending on context)
    qword_t x[31];

    // Stack pointer - can be different from x31 accesses
    qword_t sp;

    // Program counter
    qword_t pc;

    // Processor State (PSTATE) - condition flags and other state
    // N, Z, C, V flags are in bits 31:28 of PSTATE when viewed as CPSR
    union {
        qword_t pstate;
        struct {
            // Bits 0-27: Various control bits, mostly unused in user space
            bitfield _pad0:28;
            // Bit 28: V (overflow) flag
            bitfield v:1;
            // Bit 29: C (carry) flag
            bitfield c:1;
            // Bit 30: Z (zero) flag
            bitfield z:1;
            // Bit 31: N (negative) flag
            bitfield n:1;
            // Bits 32+: Mode bits and other EL0 state
            bitfield _pad1:32;
        };
    };

    // SIMD/FP registers v0-v31 (128-bit each)
    // Named 'vregs' to avoid conflict with flag aliases
    union vec_reg vregs[32];

    // Floating Point Control Register (FPCR)
    // Controls FP rounding mode, exceptions, etc.
    dword_t fpcr;

    // Floating Point Status Register (FPSR)
    // Records FP exceptions and condition flags
    dword_t fpsr;

    // Thread Local Storage register
    // TPIDR_EL0 - holds thread pointer for user space
    qword_t tpidr_el0;

    // Memory access info for page faults
    addr_t fault_addr;
    bool fault_was_write;

    // For signaling/interrupt handling
    bool *poked_ptr;
    bool _poked;
    
    // Persistent execution context for PR 1 optimization
    // Eliminates per-run allocations in cpu_step_to_interrupt
    struct fiber_exec_ctx *exec_ctx;
    
    // Trap/interrupt number for signal handling
    int trapno;
    
    // TCTI exit reason - set by assembly code before returning
    int tcti_exit_reason;
    
    // TLB pointer for inline TLB lookup in TCTI gadgets
    struct tlb *tlb;
};

#define CPU_OFFSET(field) offsetof(struct cpu_state, field)

// TLB structure offsets for inline TLB lookup in assembly
// These must match the actual struct layouts
#define CPU_TLB_OFFSET          CPU_OFFSET(tlb)          // Offset of tlb pointer in cpu_state
#define TLB_ENTRIES_OFFSET      32                      // Offset of entries in struct tlb
#define TLB_ENTRY_SIZE          16                      // Size of each tlb_entry (page:4 + page_if_writable:4 + data_minus_addr:8)
#define TLB_ENTRY_PAGE_OFFSET   0                       // Offset of page within tlb_entry
#define TLB_ENTRY_DATA_OFFSET   8                       // Offset of data_minus_addr within tlb_entry
#define PAGE_BITS               12                      // Page size is 4KB

// Verify struct layout assumptions
static_assert(CPU_OFFSET(x[0]) == offsetof(struct cpu_state, x), "x array offset");
static_assert(sizeof(struct cpu_state) < 0xffff, "cpu struct is too big for gadgets");

// Register name enums for debugging
enum reg64 {
    reg_x0 = 0, reg_x1, reg_x2, reg_x3, reg_x4, reg_x5, reg_x6, reg_x7,
    reg_x8, reg_x9, reg_x10, reg_x11, reg_x12, reg_x13, reg_x14, reg_x15,
    reg_x16, reg_x17, reg_x18, reg_x19, reg_x20, reg_x21, reg_x22, reg_x23,
    reg_x24, reg_x25, reg_x26, reg_x27, reg_x28, reg_x29, reg_x30,
    reg_count,
    reg_none = reg_count,
    reg_sp = reg_count + 1,
};

static inline const char *reg64_name(enum reg64 reg) {
    switch (reg) {
        case reg_x0: return "x0";
        case reg_x1: return "x1";
        case reg_x2: return "x2";
        case reg_x3: return "x3";
        case reg_x4: return "x4";
        case reg_x5: return "x5";
        case reg_x6: return "x6";
        case reg_x7: return "x7";
        case reg_x8: return "x8";
        case reg_x9: return "x9";
        case reg_x10: return "x10";
        case reg_x11: return "x11";
        case reg_x12: return "x12";
        case reg_x13: return "x13";
        case reg_x14: return "x14";
        case reg_x15: return "x15";
        case reg_x16: return "x16";
        case reg_x17: return "x17";
        case reg_x18: return "x18";
        case reg_x19: return "x19";
        case reg_x20: return "x20";
        case reg_x21: return "x21";
        case reg_x22: return "x22";
        case reg_x23: return "x23";
        case reg_x24: return "x24";
        case reg_x25: return "x25";
        case reg_x26: return "x26";
        case reg_x27: return "x27";
        case reg_x28: return "x28";
        case reg_x29: return "x29";  // Frame pointer
        case reg_x30: return "x30";  // Link register
        case reg_sp: return "sp";
        default: return "?";
    }
}

// Helper to get/set xN or wN (32-bit view of register)
static inline qword_t get_xn(struct cpu_state *cpu, int n) {
    if (n >= 0 && n < 31)
        return cpu->x[n];
    return 0;
}

static inline void set_xn(struct cpu_state *cpu, int n, qword_t val) {
    if (n >= 0 && n < 31)
        cpu->x[n] = val;
}

static inline dword_t get_wn(struct cpu_state *cpu, int n) {
    if (n >= 0 && n < 31)
        return (dword_t)cpu->x[n];
    return 0;
}

static inline void set_wn(struct cpu_state *cpu, int n, dword_t val) {
    if (n >= 0 && n < 31)
        cpu->x[n] = (qword_t)val;  // Zero extend to 64-bit
}

// Condition flag helpers matching NZCV layout
#define A64_N (cpu->n)
#define A64_Z (cpu->z)
#define A64_C (cpu->c)
#define A64_V (cpu->v)

// Update all flags from result
static inline void set_nzcv(struct cpu_state *cpu, qword_t result, int is_64bit) {
    cpu->z = (result == 0);
    cpu->n = is_64bit ? (result >> 63) & 1 : (result >> 31) & 1;
}

// Set flags for logical operations (N, Z from result, C/V unchanged)
static inline void set_nz_logical(struct cpu_state *cpu, qword_t result, int is_64bit) {
    cpu->z = (result == 0);
    cpu->n = is_64bit ? (result >> 63) & 1 : (result >> 31) & 1;
    // C and V are preserved
}

// Set flags for arithmetic operations
static inline void set_nzcv_arith(struct cpu_state *cpu, qword_t result,
                                   qword_t op1, qword_t op2, int is_add, int is_64bit) {
    cpu->z = (result == 0);
    cpu->n = is_64bit ? (result >> 63) & 1 : (result >> 31) & 1;

    if (is_64bit) {
        if (is_add) {
            cpu->c = (result < op1);  // Unsigned overflow
            // Signed overflow: sign of result different from sign of operands
            cpu->v = ((~(op1 ^ op2) & (op1 ^ result)) >> 63) & 1;
        } else {
            cpu->c = (op1 >= op2);  // Unsigned underflow (no borrow)
            // Signed underflow
            cpu->v = (((op1 ^ op2) & (op1 ^ result)) >> 63) & 1;
        }
    } else {
        // 32-bit operations
        dword_t r32 = (dword_t)result;
        dword_t a32 = (dword_t)op1;
        dword_t b32 = (dword_t)op2;
        if (is_add) {
            cpu->c = (r32 < a32);
            cpu->v = ((~(a32 ^ b32) & (a32 ^ r32)) >> 31) & 1;
        } else {
            cpu->c = (a32 >= b32);
            cpu->v = (((a32 ^ b32) & (a32 ^ r32)) >> 31) & 1;
        }
    }
}

// Instruction fetch for TCTI block generator
int a64_fetch_insn(struct cpu_state *cpu, struct tlb *tlb, uint64_t pc, uint32_t *insn);

// External TCTI exit gadget
extern void tcti_exit_block(int reason);

#endif
