#ifndef EMU_AARCH64_CPU_H
#define EMU_AARCH64_CPU_H

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/util/misc.h>

#ifdef __KERNEL__
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

struct cpu_state;
struct task;
struct tlb;
int cpu_run_to_interrupt(struct cpu_state *cpu, struct tlb *tlb);
void cpu_poke(struct cpu_state *cpu);

// TCTI execution functions
void a64_cpu_init(struct task *task, struct cpu_state *cpu, int err);
void a64_cpu_init_probe(struct task *task, struct cpu_state *cpu, int err);
void a64_cpu_run(struct cpu_state *cpu, struct tlb *tlb);
void a64_cpu_run_limited(struct cpu_state *cpu, struct tlb *tlb, int max_iterations);
void a64_cpu_dump(struct cpu_state *cpu);
void a64_cpu_dump_stats(struct cpu_state *cpu);
int a64_execute_ldst(struct cpu_state *cpu, struct tlb *tlb, const a64_instr_t *instr);

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
    uint64_t d[2];
    uint32_t s[4];
    uint16_t h[8];
    uint8_t b[16];
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
    uint64_t x[31];

    // Stack pointer - can be different from x31 accesses
    uint64_t sp;

    // Program counter
    uint64_t pc;

    // Processor State (PSTATE) - condition flags and other state
    // N, Z, C, V flags are in bits 31:28 of PSTATE when viewed as CPSR
    union {
        uint64_t pstate;
        struct {
            // Bits 0-27: Various control bits, mostly unused in user space
            bitfield _pad0 : 28;
            // Bit 28: V (overflow) flag
            bitfield v : 1;
            // Bit 29: C (carry) flag
            bitfield c : 1;
            // Bit 30: Z (zero) flag
            bitfield z : 1;
            // Bit 31: N (negative) flag
            bitfield n : 1;
            // Bits 32+: Mode bits and other EL0 state
            bitfield _pad1 : 32;
        };
    };

    // SIMD/FP registers v0-v31 (128-bit each)
    // Named 'vregs' to avoid conflict with flag aliases
    union vec_reg vregs[32];

    // Floating Point Control Register (FPCR)
    // Controls FP rounding mode, exceptions, etc.
    uint32_t fpcr;

    // Floating Point Status Register (FPSR)
    // Records FP exceptions and condition flags
    uint32_t fpsr;

    // Thread Local Storage register
    // TPIDR_EL0 - holds thread pointer for user space
    uint64_t tpidr_el0;

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

    // Exclusive monitor state for atomic operations (ldxr/stxr)
    // Per-CPU state instead of process-global for correctness
    uint64_t exclusive_addr; // Address being monitored
    int exclusive_size;      // Size of monitored region (1, 2, 4, or 8 bytes)
    int exclusive_valid;     // Whether monitor is valid (1) or cleared (0)

    // Phase 1B statistics counters for data-driven optimization
    // Used to guide next optimization priorities based on actual execution patterns
    uint64_t stat_ldr_fast_hits; // LDR fast-path successful completions
    uint64_t stat_str_fast_hits; // STR fast-path successful completions
    uint64_t stat_ldr_fallback;  // LDR fallback to C helper (total)
    uint64_t stat_str_fallback;  // STR fallback to C helper (total)

    // LDR fallback reason counters (index by reason enum)
    uint64_t stat_ldr_fallback_nonhot;  // 0: non-hot registers
    uint64_t stat_ldr_fallback_size;    // 1: non-64-bit size
    uint64_t stat_ldr_fallback_idxmode; // 2: non-offset indexing mode
    uint64_t stat_ldr_fallback_meta;    // 3: non-zero meta
    uint64_t stat_ldr_fallback_align;   // 4: unaligned access
    uint64_t stat_ldr_fallback_crosspg; // 5: cross-page access
    uint64_t stat_ldr_fallback_tlbmiss; // 6: TLB miss
    uint64_t stat_ldr_fallback_notlb;   // 7: no TLB attached

    // STR fallback reason counters (index by reason enum)
    uint64_t stat_str_fallback_nonhot;  // 0: non-hot registers
    uint64_t stat_str_fallback_size;    // 1: non-64-bit size
    uint64_t stat_str_fallback_idxmode; // 2: non-offset indexing mode
    uint64_t stat_str_fallback_meta;    // 3: non-zero meta
    uint64_t stat_str_fallback_align;   // 4: unaligned access
    uint64_t stat_str_fallback_crosspg; // 5: cross-page access
    uint64_t stat_str_fallback_tlbmiss; // 6: TLB miss
    uint64_t stat_str_fallback_notlb;   // 7: no TLB attached
};

#define CPU_OFFSET(field) offsetof(struct cpu_state, field)

// TLB structure offsets for inline TLB lookup in assembly
// These must match the actual struct layouts
#define CPU_TLB_OFFSET     CPU_OFFSET(tlb) // Offset of tlb pointer in cpu_state
#define TLB_ENTRIES_OFFSET 32              // Offset of entries in struct tlb
#define TLB_ENTRY_SIZE                                                                             \
    24 // Size of each tlb_entry (page:8 + page_if_writable:8 + data_minus_addr:8)
#define TLB_ENTRY_PAGE_OFFSET 0  // Offset of page within tlb_entry
#define TLB_ENTRY_DATA_OFFSET 16 // Offset of data_minus_addr within tlb_entry
#define PAGE_BITS             12 // Page size is 4KB

// Statistics counter offsets for inline increment in assembly gadgets
// These are used by gadget_ldr_x_impl and gadget_str_x_impl
#define STAT_LDR_FAST_HITS_OFFSET CPU_OFFSET(stat_ldr_fast_hits)
#define STAT_STR_FAST_HITS_OFFSET CPU_OFFSET(stat_str_fast_hits)
#define STAT_LDR_FALLBACK_OFFSET  CPU_OFFSET(stat_ldr_fallback)
#define STAT_STR_FALLBACK_OFFSET  CPU_OFFSET(stat_str_fallback)

// Fallback reason codes for data-driven optimization
// These match the counter array indices
enum tcti_fallback_reason {
    TCTI_FALLBACK_NONHOT = 0, // Non-hot registers (Rt/Rn not in 0-15)
    TCTI_FALLBACK_SIZE,       // Non-64-bit size
    TCTI_FALLBACK_IDXMODE,    // Non-offset indexing mode (writeback, etc.)
    TCTI_FALLBACK_META,       // Non-zero meta field
    TCTI_FALLBACK_ALIGN,      // Unaligned access
    TCTI_FALLBACK_CROSSPG,    // Cross-page access
    TCTI_FALLBACK_TLBMISS,    // TLB miss
    TCTI_FALLBACK_NOTLB,      // No TLB attached to CPU
    TCTI_FALLBACK_COUNT       // Number of fallback reasons
};

// Fallback counter offsets for inline increment in assembly
// Pre-calculated for efficiency in fast-path branches
#define STAT_LDR_FALLBACK_NONHOT_OFFSET  920
#define STAT_LDR_FALLBACK_SIZE_OFFSET    928
#define STAT_LDR_FALLBACK_IDXMODE_OFFSET 936
#define STAT_LDR_FALLBACK_META_OFFSET    944
#define STAT_LDR_FALLBACK_ALIGN_OFFSET   952
#define STAT_LDR_FALLBACK_CROSSPG_OFFSET 960
#define STAT_LDR_FALLBACK_TLBMISS_OFFSET 968
#define STAT_LDR_FALLBACK_NOTLB_OFFSET   976

#define STAT_STR_FALLBACK_NONHOT_OFFSET  984
#define STAT_STR_FALLBACK_SIZE_OFFSET    992
#define STAT_STR_FALLBACK_IDXMODE_OFFSET 1000
#define STAT_STR_FALLBACK_META_OFFSET    1008
#define STAT_STR_FALLBACK_ALIGN_OFFSET   1016
#define STAT_STR_FALLBACK_CROSSPG_OFFSET 1024
#define STAT_STR_FALLBACK_TLBMISS_OFFSET 1032
#define STAT_STR_FALLBACK_NOTLB_OFFSET   1040

// Verify struct layout assumptions
static_assert(CPU_OFFSET(x[0]) == offsetof(struct cpu_state, x), "x array offset");
static_assert(sizeof(struct cpu_state) < 0xffff, "cpu struct is too big for gadgets");

// Register name enums for debugging
enum reg64 {
    reg_x0 = 0,
    reg_x1,
    reg_x2,
    reg_x3,
    reg_x4,
    reg_x5,
    reg_x6,
    reg_x7,
    reg_x8,
    reg_x9,
    reg_x10,
    reg_x11,
    reg_x12,
    reg_x13,
    reg_x14,
    reg_x15,
    reg_x16,
    reg_x17,
    reg_x18,
    reg_x19,
    reg_x20,
    reg_x21,
    reg_x22,
    reg_x23,
    reg_x24,
    reg_x25,
    reg_x26,
    reg_x27,
    reg_x28,
    reg_x29,
    reg_x30,
    reg_count,
    reg_none = reg_count,
    reg_sp = reg_count + 1,
};

static inline const char *reg64_name(enum reg64 reg)
{
    switch (reg) {
    case reg_x0:
        return "x0";
    case reg_x1:
        return "x1";
    case reg_x2:
        return "x2";
    case reg_x3:
        return "x3";
    case reg_x4:
        return "x4";
    case reg_x5:
        return "x5";
    case reg_x6:
        return "x6";
    case reg_x7:
        return "x7";
    case reg_x8:
        return "x8";
    case reg_x9:
        return "x9";
    case reg_x10:
        return "x10";
    case reg_x11:
        return "x11";
    case reg_x12:
        return "x12";
    case reg_x13:
        return "x13";
    case reg_x14:
        return "x14";
    case reg_x15:
        return "x15";
    case reg_x16:
        return "x16";
    case reg_x17:
        return "x17";
    case reg_x18:
        return "x18";
    case reg_x19:
        return "x19";
    case reg_x20:
        return "x20";
    case reg_x21:
        return "x21";
    case reg_x22:
        return "x22";
    case reg_x23:
        return "x23";
    case reg_x24:
        return "x24";
    case reg_x25:
        return "x25";
    case reg_x26:
        return "x26";
    case reg_x27:
        return "x27";
    case reg_x28:
        return "x28";
    case reg_x29:
        return "x29"; // Frame pointer
    case reg_x30:
        return "x30"; // Link register
    case reg_sp:
        return "sp";
    default:
        return "?";
    }
}

// Helper to get/set xN or wN (32-bit view of register)
static inline uint64_t get_xn(struct cpu_state *cpu, int n)
{
    if (n >= 0 && n < 31)
        return cpu->x[n];
    return 0;
}

static inline void set_xn(struct cpu_state *cpu, int n, uint64_t val)
{
    if (n >= 0 && n < 31)
        cpu->x[n] = val;
}

static inline uint32_t get_wn(struct cpu_state *cpu, int n)
{
    if (n >= 0 && n < 31)
        return (uint32_t)cpu->x[n];
    return 0;
}

static inline void set_wn(struct cpu_state *cpu, int n, uint32_t val)
{
    if (n >= 0 && n < 31)
        cpu->x[n] = (uint64_t)val; // Zero extend to 64-bit
}

// Condition flag helpers matching NZCV layout
#define A64_N (cpu->n)
#define A64_Z (cpu->z)
#define A64_C (cpu->c)
#define A64_V (cpu->v)

// Update all flags from result
static inline void set_nzcv(struct cpu_state *cpu, uint64_t result, int is_64bit)
{
    cpu->z = (result == 0);
    cpu->n = is_64bit ? (result >> 63) & 1 : (result >> 31) & 1;
}

// Set flags for logical operations (N, Z from result, C/V unchanged)
static inline void set_nz_logical(struct cpu_state *cpu, uint64_t result, int is_64bit)
{
    cpu->z = (result == 0);
    cpu->n = is_64bit ? (result >> 63) & 1 : (result >> 31) & 1;
    // C and V are preserved
}

// Set flags for arithmetic operations
static inline void set_nzcv_arith(struct cpu_state *cpu, uint64_t result, uint64_t op1,
                                  uint64_t op2, int is_add, int is_64bit)
{
    cpu->z = (result == 0);
    cpu->n = is_64bit ? (result >> 63) & 1 : (result >> 31) & 1;

    if (is_64bit) {
        if (is_add) {
            cpu->c = (result < op1); // Unsigned overflow
            // Signed overflow: sign of result different from sign of operands
            cpu->v = ((~(op1 ^ op2) & (op1 ^ result)) >> 63) & 1;
        } else {
            cpu->c = (op1 >= op2); // Unsigned underflow (no borrow)
            // Signed underflow
            cpu->v = (((op1 ^ op2) & (op1 ^ result)) >> 63) & 1;
        }
    } else {
        // 32-bit operations
        uint32_t r32 = (uint32_t)result;
        uint32_t a32 = (uint32_t)op1;
        uint32_t b32 = (uint32_t)op2;
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
