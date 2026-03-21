# TCTI AArch64 Emulator Implementation Plan

## Executive Summary

This document provides a detailed implementation plan for building a high-performance, non-JIT AArch64 emulator for iOS using QEMU's TCTI (Tiny-Code Threaded Interpreter) design pattern. The emulator runs guest AArch64 Linux binaries on Darwin/iOS through a linux-user style runtime.

**Key Design Decisions:**
- **Non-JIT Default**: Uses pre-compiled gadget banks (TCTI) to avoid JIT entitlements on iOS
- **Minimal Exits**: Optimized for long chains of gadget execution with rare C helper calls
- **Linux ABI Bridge**: Complete syscall translation layer from Linux to Darwin
- **Persistent Context**: Zero steady-state allocations in the hot path

---

## 1. Architecture Overview

### 1.1 Four-Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 4: Guest Memory Manager                               │
│ - Guest VA reservation and mapping                          │
│ - Page-granular VMA tracking                                │
│ - Software TLB for fast translation                         │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│ Layer 3: Linux ABI Bridge                                   │
│ - Syscall classification and routing                        │
│ - Struct marshalling (Linux ↔ Darwin)                       │
│ - Signal synthesis and delivery                             │
│ - Thread and futex emulation                                │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│ Layer 2: TB Cache & Chaining                                │
│ - L0: Per-thread jump cache (direct-mapped)                 │
│ - L1: Global TB hash table (lock-free lookups)              │
│ - Direct block chaining between TBs                         │
│ - Epoch-based reclamation                                   │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│ Layer 1: Guest Execution Core (TCTI)                        │
│ - AArch64 instruction decoding                              │
│ - IR lowering and TCTI stream emission                      │
│ - Pre-compiled gadget bank in binary                        │
│ - x28 thread-stream pointer traversal                       │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Execution Flow

```
Guest PC Entry
       │
       ▼
┌──────────────┐     Miss     ┌──────────────┐
│   L0 Cache   │───────────────▶│   L1 Cache   │
│  (512 slots) │                │(Global Hash) │
└──────┬───────┘                └──────┬───────┘
       │                               │
       │ Hit                           │ Miss
       │                               ▼
       │                        ┌──────────────┐
       │                        │   Decoder    │
       │                        │      +       │
       │                        │ TCTI Emitter │
       │                        └──────┬───────┘
       │                               │
       ▼                               ▼
┌──────────────┐                ┌──────────────┐
│   TCTI Stream │◀───────────────│ Insert to L1 │
│  Execution   │                └──────────────┘
└──────┬───────┘
       │
       │ Chainable Exit
       ▼
┌──────────────┐
│ Direct Patch │
│ Jump Slot    │
└──────┬───────┘
       │
       ▼
Next TB (no dispatcher)
```

---

## 2. Data Structures & Headers

### 2.1 Core Data Structures

**File: `include/tcti/core.h`**

```c
#ifndef TCTI_CORE_H
#define TCTI_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// Architecture constants
#define TCTI_GADGET_BANK_SIZE      (1 << 16)  // 64K gadgets max
#define TCTI_STREAM_MAX_SIZE       4096
#define TCTI_CONSTANT_POOL_SIZE    1024
#define TB_L0_CACHE_SIZE           512
#define TB_L1_HASH_SIZE            (1 << 20)  // 1M entries
#define GUEST_VA_ARENA_SIZE        (1ULL << 36)  // 64GB guest VA

// Forward declarations
typedef struct Tb Tb;
typedef struct CpuThread CpuThread;
typedef struct GuestThreadState GuestThreadState;
typedef struct TlbEntry TlbEntry;

// TB Cache Key - minimal state affecting translation
typedef struct {
    uint64_t guest_pc;
    uint64_t state_hash;    // MMU idx, priv level, etc.
    uint32_t page_gen;
    uint32_t flags;
} TbKey;

// Translation Block - compact representation
struct Tb {
    TbKey key;
    uint64_t guest_end_pc;
    uint32_t stream_off;     // Offset into global stream arena
    uint32_t stream_len;     // Number of 32-bit entries
    uint32_t const_off;      // Offset into constant pool
    uint32_t const_len;      // Number of 64-bit constants
    uint32_t guest_page_lo;  // First guest page covered
    uint32_t guest_page_hi;  // Last guest page covered
    uint32_t epoch;          // Creation epoch for reclamation
    uint16_t flags;
    uint16_t exit_count;     // Number of exit points
    
    // Chain slots for direct linking
    uint64_t *chain_slots;   // Patchable jump targets
    uint32_t chain_count;
    
    // Reverse mapping for invalidation
    struct Tb **prev_tbs;    // TBs that chain to this one
    uint32_t prev_count;
    uint32_t prev_cap;
};

// L0 Cache Entry (per-thread, direct-mapped)
typedef struct {
    uint64_t guest_pc;
    uint64_t state_hash;
    Tb *tb;
} TbL0Entry;

// Execution Context (persistent per thread)
typedef struct {
    TbL0Entry l0_cache[TB_L0_CACHE_SIZE];
    Tb *current_tb;
    uint64_t pending_signals;
    uint64_t exit_reason;
    uint64_t last_guest_pc;
    uint64_t last_state_hash;
    
    // Stats counters
    uint64_t l0_hits;
    uint64_t l1_hits;
    uint64_t compiles;
    uint64_t chain_hits;
    uint64_t helper_calls;
    
    // Epoch state
    uint32_t local_epoch;
    bool active;
} FiberExecCtx;

// Guest register file (AArch64)
typedef struct {
    uint64_t x[31];          // X0-X30
    uint64_t sp;             // Stack pointer
    uint64_t pc;             // Program counter
    uint64_t pstate;         // NZCV + DAIF
    uint64_t tpidr_el0;      // TLS register
    
    // SIMD (optional, for future)
    // uint64_t v[32][2];    // 128-bit vectors
} GuestRegs;

// CPU Thread State
typedef struct CpuThread {
    GuestRegs regs;
    FiberExecCtx exec_ctx;
    
    // Guest thread metadata
    uint32_t guest_tid;
    uint32_t guest_tgid;
    uint64_t clear_child_tid;
    uint64_t robust_list_head;
    
    // Signal state
    uint64_t sigmask;
    uint64_t pending_signals;
    
    // Memory
    void *guest_arena_base;
    TlbEntry *tlb;
} CpuThread;

// TLB Entry (software-managed)
struct TlbEntry {
    uint64_t guest_page;
    uint64_t host_addr;
    uint64_t flags;          // R/W/X, dirty, etc.
    uint32_t page_gen;
    uint32_t pad;
};

// Global TB Context
typedef struct {
    Tb *l1_hash[TB_L1_HASH_SIZE];
    pthread_rwlock_t hash_lock;
    
    // Retired block reclamation
    uint32_t global_epoch;
    Tb *retired_buckets[3];  // Epoch mod 3
    pthread_mutex_t epoch_lock;
    
    // Compiled page tracking
    uint8_t *compiled_bitmap;
    size_t bitmap_size;
    
    // Memory pools
    void *stream_arena;
    size_t stream_used;
    void *constant_arena;
    size_t constant_used;
} TbContext;

// Exit reasons
#define EXIT_SYSCALL        1
#define EXIT_TLB_MISS       2
#define EXIT_CHAIN_RESOLVE  3
#define EXIT_SIGNAL         4
#define EXIT_INVALIDATE     5
#define EXIT_HELPER         6
#define EXIT_FAULT          7

#endif // TCTI_CORE_H
```

### 2.2 Syscall Bridge Interface

**File: `include/tcti/syscall_bridge.h`**

```c
#ifndef TCTI_SYSCALL_BRIDGE_H
#define TCTI_SYSCALL_BRIDGE_H

#include "core.h"

// Syscall classification
enum SyscallClass {
    SYSCALL_DIRECT_HOST,      // Light translation, direct call
    SYSCALL_STRUCT_TRANS,     // Host call with struct marshalling
    SYSCALL_USER_EMULATION,   // User-space emulation required
    SYSCALL_REJECT,           // Return -ENOSYS
};

// Linux syscall numbers (AArch64)
#define LINUX_SYS_read              63
#define LINUX_SYS_write             64
#define LINUX_SYS_close             57
#define LINUX_SYS_lseek             62
#define LINUX_SYS_mmap              222
#define LINUX_SYS_mprotect          226
#define LINUX_SYS_munmap            215
#define LINUX_SYS_brk               214
#define LINUX_SYS_exit              93
#define LINUX_SYS_exit_group        94
#define LINUX_SYS_getpid            172
#define LINUX_SYS_gettid            178
#define LINUX_SYS_clone             220
#define LINUX_SYS_futex             98
#define LINUX_SYS_openat            56
#define LINUX_SYS_newfstatat        79
#define LINUX_SYS_rt_sigaction      134
#define LINUX_SYS_rt_sigprocmask    135
#define LINUX_SYS_rt_sigreturn      139
#define LINUX_SYS_clock_gettime     113
#define LINUX_SYS_nanosleep         101
#define LINUX_SYS_ioctl             29
#define LINUX_SYS_prlimit64         261
#define LINUX_SYS_set_tid_address   96
#define LINUX_SYS_uname             160

// Syscall handler signature
typedef long (*SyscallHandler)(
    CpuThread *cpu,
    uint64_t a0, uint64_t a1, uint64_t a2,
    uint64_t a3, uint64_t a4, uint64_t a5
);

// Main dispatch function
long linux_syscall_dispatch(
    CpuThread *cpu,
    uint32_t linux_nr,
    uint64_t a0, uint64_t a1, uint64_t a2,
    uint64_t a3, uint64_t a4, uint64_t a5
);

// Classify syscall
enum SyscallClass syscall_classify(uint32_t linux_nr);

// Marshalling helpers
long linux_to_host_errno(int linux_errno);
int host_to_linux_errno(long host_errno);

// Struct conversion helpers (Linux ↔ Darwin)
int linux_to_host_timespec(uint64_t guest_addr, struct timespec *host);
int host_to_linux_timespec(struct timespec *host, uint64_t guest_addr);
int linux_to_host_stat(uint64_t guest_addr, void *host_stat);
int host_to_linux_stat(void *host_stat, uint64_t guest_addr);
int linux_to_host_sigaction(uint64_t guest_addr, void *host_act);
int host_to_linux_sigaction(void *host_act, uint64_t guest_addr);

// Guest memory access from bridge
void *guest_to_host(CpuThread *cpu, uint64_t guest_addr);
int guest_copy_from(CpuThread *cpu, void *dst, uint64_t src, size_t len);
int guest_copy_to(CpuThread *cpu, uint64_t dst, const void *src, size_t len);

// Signal handling
void signal_queue(CpuThread *cpu, int sig, siginfo_t *info);
void signal_deliver_pending(CpuThread *cpu);

// Thread management
int thread_create(CpuThread *parent, uint64_t clone_flags, 
                  uint64_t stack_addr, uint64_t ptid, uint64_t ctid);
int thread_exit(CpuThread *thread, int status);

// Futex emulation
typedef struct FutexBucket {
    uint64_t guest_addr;
    uint32_t val;
    pthread_cond_t cond;
    pthread_mutex_t lock;
    struct FutexBucket *next;
} FutexBucket;

int futex_wait(CpuThread *cpu, uint64_t uaddr, uint32_t val, 
               uint64_t timeout_addr);
int futex_wake(CpuThread *cpu, uint64_t uaddr, uint32_t nr_wake);

#endif // TCTI_SYSCALL_BRIDGE_H
```

### 2.3 Memory Manager Interface

**File: `include/tcti/guest_memory.h`**

```c
#ifndef TCTI_GUEST_MEMORY_H
#define TCTI_GUEST_MEMORY_H

#include "core.h"

// Guest page flags
#define GUEST_PAGE_VALID    (1 << 0)
#define GUEST_PAGE_READ     (1 << 1)
#define GUEST_PAGE_WRITE    (1 << 2)
#define GUEST_PAGE_EXEC     (1 << 3)
#define GUEST_PAGE_COW      (1 << 4)
#define GUEST_PAGE_FILE     (1 << 5)
#define GUEST_PAGE_DIRTY    (1 << 6)

// VMA (Virtual Memory Area)
typedef struct GuestVma {
    uint64_t start;
    uint64_t end;
    uint32_t flags;
    uint32_t page_gen;
    void *host_backing;      // NULL if not backed
    char *filename;          // For file mappings
    uint64_t offset;
    struct GuestVma *left;
    struct GuestVma *right;
} GuestVma;

// Guest Memory Manager
typedef struct {
    CpuThread *cpu;
    void *arena_base;
    size_t arena_size;
    GuestVma *vma_root;
    pthread_rwlock_t vma_lock;
    
    // TLB
    TlbEntry *tlb;
    size_t tlb_size;
    uint32_t tlb_gen;
    
    // Stats
    uint64_t tlb_hits;
    uint64_t tlb_misses;
    uint64_t page_faults;
} GuestMm;

// Initialize guest memory
GuestMm *guest_mm_init(CpuThread *cpu, size_t arena_size);
void guest_mm_destroy(GuestMm *mm);

// VMA operations
GuestVma *guest_mm_find_vma(GuestMm *mm, uint64_t addr);
int guest_mm_map(GuestMm *mm, uint64_t addr, size_t len, 
                 uint32_t flags, void *host_backing);
int guest_mm_unmap(GuestMm *mm, uint64_t addr, size_t len);
int guest_mm_protect(GuestMm *mm, uint64_t addr, size_t len, uint32_t flags);

// TLB operations
static inline TlbEntry *guest_mm_tlb_lookup(GuestMm *mm, uint64_t guest_page) {
    size_t idx = (guest_page >> 12) & (mm->tlb_size - 1);
    TlbEntry *e = &mm->tlb[idx];
    if (e->guest_page == guest_page && e->page_gen == mm->tlb_gen) {
        mm->tlb_hits++;
        return e;
    }
    mm->tlb_misses++;
    return NULL;
}

void guest_mm_tlb_flush(GuestMm *mm);
void guest_mm_tlb_flush_page(GuestMm *mm, uint64_t guest_page);

// Address translation
void *guest_to_host_fast(GuestMm *mm, uint64_t guest_addr);
void *guest_to_host_safe(GuestMm *mm, uint64_t guest_addr, uint32_t access);

// Page fault handling
int guest_mm_handle_fault(GuestMm *mm, uint64_t addr, uint32_t access);

#endif // TCTI_GUEST_MEMORY_H
```

### 2.4 Gadget Bank Interface

**File: `include/tcti/gadget_bank.h`**

```c
#ifndef TCTI_GADGET_BANK_H
#define TCTI_GADGET_BANK_H

#include <stdint.h>

// Gadget function type
typedef void (*GadgetFn)(void);

// Gadget categories
enum GadgetCategory {
    GADGET_ARITH,       // add, sub, cmp, etc.
    GADGET_LOGICAL,     // and, orr, eor, etc.
    GADGET_SHIFT,       // lsl, lsr, asr, etc.
    GADGET_MOVE,        // mov, mvn, etc.
    GADGET_BRANCH,      // b, bl, ret, etc.
    GADGET_COND,        // csel, cset, etc.
    GADGET_LOAD,        // ldr, ldp, etc.
    GADGET_STORE,       // str, stp, etc.
    GADGET_MEMORY,      // Memory helpers
    GADGET_SYSTEM,      // svc, mrs, msr, etc.
    GADGET_FLAG,        // Flag handling
    GADGET_HELPER,      // C helper calls
};

// Gadget lookup by opcode pattern
GadgetFn gadget_lookup_arith(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t op);
GadgetFn gadget_lookup_logical(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t op);
GadgetFn gadget_lookup_shift(uint8_t rd, uint8_t rn, uint8_t shift, uint8_t amt);
GadgetFn gadget_lookup_move(uint8_t rd, uint8_t rn, uint8_t kind);
GadgetFn gadget_lookup_branch(uint64_t target);
GadgetFn gadget_lookup_cond(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t cond, uint8_t op);
GadgetFn gadget_lookup_load(uint8_t rt, uint8_t rn, int32_t offset, uint8_t size);
GadgetFn gadget_lookup_store(uint8_t rt, uint8_t rn, int32_t offset, uint8_t size);
GadgetFn gadget_lookup_system(uint32_t opcode);
GadgetFn gadget_lookup_helper(uint32_t helper_id);

// Gadget bank initialization
void gadget_bank_init(void);
GadgetFn gadget_bank_get(uint32_t gadget_id);

// Pre-compiled gadget counts (generated at build time)
#define GADGET_ARITH_COUNT      4096
#define GADGET_LOGICAL_COUNT    4096
#define GADGET_SHIFT_COUNT      1024
#define GADGET_MOVE_COUNT       256
#define GADGET_COND_COUNT       2048
#define GADGET_LOAD_COUNT       8192
#define GADGET_STORE_COUNT      8192

#endif // TCTI_GADGET_BANK_H
```

---

## 3. File-by-File Implementation

### 3.1 Core Execution Layer

**File: `src/core/tcti_exec.c`** (2,500 lines)

**Purpose**: Main execution loop, TB lookup, and TCTI stream execution

**Key Functions**:
```c
// Initialize execution context for a thread
void tcti_exec_ctx_init(FiberExecCtx *ctx);

// Main execution entry point - runs until interrupt/exit
void tcti_run(CpuThread *cpu, uint64_t guest_pc);

// L0 cache lookup (hot path, inline)
static inline Tb *tcti_l0_lookup(FiberExecCtx *ctx, uint64_t pc, uint64_t hash);

// L1 hash lookup with lock-free read path
Tb *tcti_l1_lookup(TbContext *tbctx, TbKey *key);

// TB translation on miss
Tb *tcti_translate(TbContext *tbctx, CpuThread *cpu, uint64_t guest_pc);

// Direct chain patching
void tcti_patch_chain(Tb *from, int exit_idx, Tb *to);

// Epoch-based reclamation
void tcti_retire_tb(TbContext *tbctx, Tb *tb);
void tcti_reclaim_epoch(TbContext *tbctx, uint32_t epoch);
```

**Implementation Notes**:
- L0 cache is 512-entry direct-mapped, indexed by `(pc ^ hash) & 511`
- L1 uses robin-hood hashing with lock-free lookups
- Translation stops at: control flow, syscall, page boundary, or 256 instructions
- Epoch reclamation uses 3 buckets (mod 3), frees only when no active thread in old epoch

**Dependencies**: `core.h`, `decoder.h`, `gadget_bank.h`, `guest_memory.h`

**Build Order**: 3 (after decoder, before syscall bridge)

---

**File: `src/core/decoder.c`** (3,000 lines)

**Purpose**: AArch64 instruction decoding and IR generation

**Key Functions**:
```c
// Decode single instruction
typedef enum {
    OP_ADD, OP_SUB, OP_AND, OP_ORR, OP_EOR,
    OP_LSL, OP_LSR, OP_ASR, OP_ROR,
    OP_MOV, OP_MVN, OP_CMP, OP_CMN,
    OP_CSEL, OP_CSET, OP_CSINC, OP_CSINV,
    OP_LDR, OP_STR, OP_LDP, OP_STP,
    OP_B, OP_BL, OP_RET, OP_BR, OP_BLR,
    OP_CBZ, OP_CBNZ, OP_TBZ, OP_TBNZ,
    OP_SVC, OP_MRS, OP_MSR, OP_NOP,
    OP_FLAG_SET, OP_FLAG_USE,
    OP_INVALID
} IrOp;

typedef struct {
    IrOp op;
    uint8_t rd, rn, rm;
    uint64_t imm;
    uint8_t shift;
    uint8_t cond;
    uint8_t size;  // For memory ops
    bool is_64bit;
    bool sets_flags;
} IrInst;

// Decode instruction at address
int decode_instruction(uint32_t insn, IrInst *out);

// Decode basic block
int decode_basic_block(CpuThread *cpu, uint64_t start_pc, 
                       IrInst *buffer, int max_inst,
                       uint64_t *end_pc, bool *needs_helper);

// Lower IR to TCTI stream
int lower_to_tcti(IrInst *inst, int count, 
                  uint32_t *stream, int *stream_len,
                  uint64_t *constants, int *const_len);
```

**Implementation Notes**:
- Decode tables generated from ARM ARM (AArch64)
- Invalid instruction (>15 bytes) hardening
- Flag optimization: track flag consumers, defer materialization
- Stop conditions: branch, exception, syscall, page boundary, 256 insts

**Dependencies**: `core.h`

**Build Order**: 2 (first after headers)

---

### 3.2 Gadget Bank Layer

**File: `src/gadgets/gadget_bank_aarch64.S`** (5,000 lines assembly)

**Purpose**: Pre-compiled gadget implementations for AArch64

**Register Usage Convention**:
- x0-x16: Guest registers (x0-x15 mapped, x16 temp)
- x17: Scratch
- x18-x25: Reserved for platform
- x26: Thread-local pointer (cpu)
- x27: Next gadget address
- x28: Thread-stream pointer
- x29: Frame pointer (unused in gadgets)
- x30: Link register (return to C)

**Gadget Categories**:

```assembly
// Example: ADD register gadget
// Entry: x28 points to next gadget address
.global gadget_add_reg_x0_x1_x2
gadget_add_reg_x0_x1_x2:
    add x0, x1, x2
    ldr x27, [x28], #8      // Load next gadget addr, advance stream
    br x27                  // Jump to next

// Example: Conditional branch resolution
.global gadget_bcond_eq_resolve
gadget_bcond_eq_resolve:
    // Check NZCV
    mrs x17, nzcv
    // Test EQ condition
    and x17, x17, #0x40000000  // Z flag
    cbz x17, 1f
    // Taken: load target from stream
    ldr x27, [x28], #8
    br x27
1:  // Not taken: skip target, continue
    add x28, x28, #8
    ldr x27, [x28], #8
    br x27

// Example: Memory load with TLB fast path
.global gadget_ldr_x0_x1_offset_0
gadget_ldr_x0_x1_offset_0:
    // Compute guest address
    add x17, x1, #0
    // TLB lookup (inline)
    lsr x18, x17, #12           // Guest page
    and x19, x18, #1023         // TLB index
    ldr x20, [x26, #TLB_OFFSET] // TLB base
    add x20, x20, x19, lsl #4   // Entry addr
    ldr x21, [x20]              // Guest page
    cmp x21, x18
    b.ne tlb_miss               // Slow path
    ldr x22, [x20, #8]          // Host addr
    add x22, x22, x17
    ldr x0, [x22]               // Actual load
    ldr x27, [x28], #8
    br x27
tlb_miss:
    // Save state, call C handler
    str x17, [x26, #FAULT_ADDR]
    mov x0, x26
    bl tlb_handle_miss
    // Resume or exit
```

**Implementation Notes**:
- All gadgets end with `ldr x27, [x28], #8; br x27`
- Memory gadgets include inline TLB fast path
- Helper call gadgets save/restore state
- Generated by Python script at build time

**Dependencies**: None (raw assembly)

**Build Order**: 1 (compiled to .o first)

---

**File: `src/gadgets/gadget_gen.py`** (800 lines)

**Purpose**: Generate gadget bank source from templates

**Generated Files**:
- `gadget_bank_arith.inc` - Arithmetic gadgets
- `gadget_bank_logic.inc` - Logical gadgets  
- `gadget_bank_memory.inc` - Load/store gadgets
- `gadget_bank_branch.inc` - Branch gadgets
- `gadget_table.c` - Lookup tables

**Build Integration**:
```python
# In meson.build:
gadget_gen = custom_target('gadget_bank',
    input: 'gadget_templates.json',
    output: ['gadget_bank.S', 'gadget_table.c'],
    command: [python3, 'src/gadgets/gadget_gen.py', 
              '@INPUT@', '@OUTPUT@']
)
```

**Build Order**: 0 (run before compilation)

---

### 3.3 Memory Management Layer

**File: `src/memory/guest_mm.c`** (2,000 lines)

**Purpose**: Guest virtual memory management and TLB

**Key Functions**:
```c
// Initialize guest memory arena
GuestMm *guest_mm_init(CpuThread *cpu, size_t arena_size);

// VMA tree operations (red-black tree)
GuestVma *vma_find(GuestMm *mm, uint64_t addr);
int vma_insert(GuestMm *mm, GuestVma *vma);
int vma_remove(GuestMm *mm, uint64_t start, size_t len);

// Page fault handling
int guest_mm_page_fault(GuestMm *mm, uint64_t addr, uint32_t access);

// mmap/munmap/mprotect emulation
int guest_mmap(CpuThread *cpu, uint64_t addr, size_t len, 
               int prot, int flags, int fd, off_t offset);
int guest_munmap(CpuThread *cpu, uint64_t addr, size_t len);
int guest_mprotect(CpuThread *cpu, uint64_t addr, size_t len, int prot);

// COW (Copy-on-Write) handling
int guest_mm_cow_break(GuestMm *mm, uint64_t addr);

// TLB management
void tlb_invalidate_all(GuestMm *mm);
void tlb_invalidate_page(GuestMm *mm, uint64_t guest_page);
void tlb_invalidate_range(GuestMm *mm, uint64_t start, uint64_t end);
```

**Implementation Notes**:
- Reserve 64GB guest VA at startup (mmap with MAP_NORESERVE)
- VMA tree tracks permissions, backing, COW state
- TLB is 1024-entry direct-mapped with 2-way optional
- Page faults: check VMA, allocate backing, update TLB

**Dependencies**: `core.h`, `guest_memory.h`

**Build Order**: 4 (after core)

---

**File: `src/memory/tlb.c`** (800 lines)

**Purpose**: Software TLB management

**Key Functions**:
```c
// Fast path TLB fill
static inline void *tlb_translate_fast(GuestMm *mm, uint64_t addr, 
                                        uint32_t access, bool *hit);

// Slow path TLB miss handling
void *tlb_handle_miss(CpuThread *cpu, uint64_t addr, uint32_t access);

// Cross-page access handling
void *tlb_handle_cross_page(CpuThread *cpu, uint64_t addr, 
                            uint32_t size, uint32_t access);
```

**Implementation Notes**:
- Inline assembly for hot path in AArch64 gadgets
- Miss handler walks VMA tree, checks permissions
- Cross-page access splits operation

**Dependencies**: `guest_memory.h`

**Build Order**: 4 (with guest_mm.c)

---

### 3.4 Syscall Bridge Layer

**File: `src/syscall/syscall_dispatch.c`** (1,500 lines)

**Purpose**: Main syscall dispatch and classification

**Key Functions**:
```c
// Syscall dispatch table
static SyscallHandler syscall_table[] = {
    [LINUX_SYS_read] = sys_read,
    [LINUX_SYS_write] = sys_write,
    [LINUX_SYS_close] = sys_close,
    [LINUX_SYS_lseek] = sys_lseek,
    [LINUX_SYS_mmap] = sys_mmap,
    [LINUX_SYS_munmap] = sys_munmap,
    [LINUX_SYS_mprotect] = sys_mprotect,
    [LINUX_SYS_brk] = sys_brk,
    [LINUX_SYS_exit] = sys_exit,
    [LINUX_SYS_exit_group] = sys_exit_group,
    [LINUX_SYS_getpid] = sys_getpid,
    [LINUX_SYS_gettid] = sys_gettid,
    [LINUX_SYS_clone] = sys_clone,
    [LINUX_SYS_futex] = sys_futex,
    [LINUX_SYS_openat] = sys_openat,
    [LINUX_SYS_newfstatat] = sys_newfstatat,
    [LINUX_SYS_rt_sigaction] = sys_rt_sigaction,
    [LINUX_SYS_rt_sigprocmask] = sys_rt_sigprocmask,
    [LINUX_SYS_clock_gettime] = sys_clock_gettime,
    [LINUX_SYS_nanosleep] = sys_nanosleep,
    [LINUX_SYS_ioctl] = sys_ioctl,
};

// Classify syscall for routing
enum SyscallClass syscall_classify(uint32_t nr) {
    switch (nr) {
        case LINUX_SYS_read:
        case LINUX_SYS_write:
        case LINUX_SYS_close:
        case LINUX_SYS_getpid:
        case LINUX_SYS_gettid:
        case LINUX_SYS_clock_gettime:
            return SYSCALL_DIRECT_HOST;
            
        case LINUX_SYS_openat:
        case LINUX_SYS_newfstatat:
        case LINUX_SYS_mmap:
        case LINUX_SYS_ioctl:
            return SYSCALL_STRUCT_TRANS;
            
        case LINUX_SYS_clone:
        case LINUX_SYS_futex:
            return SYSCALL_USER_EMULATION;
            
        case LINUX_SYS_exit:
        case LINUX_SYS_exit_group:
            return SYSCALL_USER_EMULATION;
            
        default:
            return SYSCALL_REJECT;
    }
}

// Main dispatch
long linux_syscall_dispatch(CpuThread *cpu, uint32_t nr,
    uint64_t a0, uint64_t a1, uint64_t a2,
    uint64_t a3, uint64_t a4, uint64_t a5) {
    
    enum SyscallClass class = syscall_classify(nr);
    long result;
    
    switch (class) {
        case SYSCALL_DIRECT_HOST:
            result = syscall_table[nr](cpu, a0, a1, a2, a3, a4, a5);
            break;
            
        case SYSCALL_STRUCT_TRANS:
            result = syscall_with_marshalling(cpu, nr, a0, a1, a2, a3, a4, a5);
            break;
            
        case SYSCALL_USER_EMULATION:
            result = syscall_emulate(cpu, nr, a0, a1, a2, a3, a4, a5);
            break;
            
        case SYSCALL_REJECT:
            result = -ENOSYS;
            break;
    }
    
    // Convert errno
    if (result < 0) {
        result = -linux_to_host_errno(-result);
    }
    
    return result;
}
```

**Implementation Notes**:
- Syscall table is static const for cache efficiency
- Classification done once at init, cached
- Marshalling centralized, not spread across handlers

**Dependencies**: `core.h`, `syscall_bridge.h`

**Build Order**: 5 (after memory layer)

---

**File: `src/syscall/syscall_file.c`** (1,200 lines)

**Purpose**: File operations with struct marshalling

**Key Functions**:
```c
// Direct passthrough with light translation
long sys_read(CpuThread *cpu, uint64_t fd, uint64_t buf, 
              uint64_t count, ...);
long sys_write(CpuThread *cpu, uint64_t fd, uint64_t buf,
               uint64_t count, ...);
long sys_close(CpuThread *cpu, uint64_t fd, ...);

// Struct translation required
long sys_openat(CpuThread *cpu, uint64_t dfd, uint64_t pathname,
                uint64_t flags, uint64_t mode, ...);
long sys_newfstatat(CpuThread *cpu, uint64_t dfd, uint64_t pathname,
                    uint64_t statbuf, uint64_t flags, ...);

// FD shadow table
typedef struct {
    int host_fd;
    uint32_t linux_flags;
    bool cloexec;
    int kind;  // REG, DIR, PIPE, etc.
} FdEntry;

FdEntry *fd_table_get(CpuThread *cpu, int guest_fd);
int fd_table_alloc(CpuThread *cpu, int host_fd, uint32_t flags);
void fd_table_free(CpuThread *cpu, int guest_fd);
```

**Implementation Notes**:
- FD shadow table maps guest fds to host fds
- Linux flags translated to Darwin flags
- Pathnames copied to temp buffer for null-termination

**Dependencies**: `syscall_bridge.h`

**Build Order**: 5 (with syscall_dispatch.c)

---

**File: `src/syscall/syscall_mmap.c`** (1,000 lines)

**Purpose**: Memory mapping operations

**Key Functions**:
```c
// mmap/munmap/mprotect
long sys_mmap(CpuThread *cpu, uint64_t addr, uint64_t len,
              uint64_t prot, uint64_t flags, uint64_t fd,
              uint64_t offset);
long sys_munmap(CpuThread *cpu, uint64_t addr, uint64_t len, ...);
long sys_mprotect(CpuThread *cpu, uint64_t addr, uint64_t len,
                  uint64_t prot, ...);

// brk emulation
long sys_brk(CpuThread *cpu, uint64_t new_brk, ...);

// Special handling for MAP_ANONYMOUS vs file-backed
int mmap_anonymous(GuestMm *mm, uint64_t addr, size_t len, uint32_t prot);
int mmap_file(GuestMm *mm, uint64_t addr, size_t len, uint32_t prot,
              int host_fd, off_t offset);
```

**Implementation Notes**:
- MAP_FIXED handling respects guest VMA constraints
- File mappings use host mmap with offset
- Anonymous mappings use mmap(MAP_ANON)

**Dependencies**: `syscall_bridge.h`, `guest_memory.h`

**Build Order**: 5

---

**File: `src/syscall/syscall_thread.c`** (1,500 lines)

**Purpose**: Thread and futex emulation

**Key Functions**:
```c
// clone emulation (limited flags)
long sys_clone(CpuThread *cpu, uint64_t flags, uint64_t stack,
               uint64_t ptid, uint64_t ctid, uint64_t tls, ...);

// Thread-local storage
long sys_set_tid_address(CpuThread *cpu, uint64_t tidptr, ...);

// Futex emulation
long sys_futex(CpuThread *cpu, uint64_t uaddr, uint64_t futex_op,
               uint64_t val, uint64_t timeout, uint64_t uaddr2,
               uint64_t val3);

// Thread management
int thread_create(CpuThread *parent, uint64_t flags, uint64_t stack,
                  uint64_t ptid, uint64_t ctid);
int thread_exit(CpuThread *thread, int status);
void thread_join(CpuThread *target);

// Futex implementation
int futex_wait(uint64_t uaddr, uint32_t val, struct timespec *timeout);
int futex_wake(uint64_t uaddr, uint32_t nr_wake);
int futex_requeue(uint64_t uaddr, uint32_t nr_wake, uint32_t nr_requeue,
                  uint64_t uaddr2);
```

**Implementation Notes**:
- One host pthread per guest thread
- Futex hash table keyed by guest address
- Clone flags limited to: CLONE_VM, CLONE_FS, CLONE_FILES, CLONE_SIGHAND, CLONE_THREAD, CLONE_SYSVSEM

**Dependencies**: `syscall_bridge.h`

**Build Order**: 5

---

**File: `src/syscall/syscall_signal.c`** (1,000 lines)

**Purpose**: Signal handling and delivery

**Key Functions**:
```c
// sigaction/sigprocmask
long sys_rt_sigaction(CpuThread *cpu, uint64_t sig, uint64_t act,
                      uint64_t oact, uint64_t sigsetsize, ...);
long sys_rt_sigprocmask(CpuThread *cpu, uint64_t how, uint64_t set,
                        uint64_t oldset, uint64_t sigsetsize, ...);
long sys_rt_sigreturn(CpuThread *cpu, ...);

// Signal queueing and delivery
void signal_queue(CpuThread *cpu, int sig, siginfo_t *info);
void signal_deliver_pending(CpuThread *cpu);
void signal_setup_frame(CpuThread *cpu, int sig, siginfo_t *info);

// Host signal redirect
void host_signal_handler(int sig, siginfo_t *info, void *uctx);
void setup_host_signal_handlers(void);
```

**Implementation Notes**:
- Signals queued per-thread, delivered at safe points
- Signal frames built in guest stack space
- sigreturn restores guest context

**Dependencies**: `syscall_bridge.h`

**Build Order**: 5

---

**File: `src/syscall/struct_marshal.c`** (1,500 lines)

**Purpose**: Linux ↔ Darwin struct conversion

**Key Functions**:
```c
// timespec/timeval
int linux_to_host_timespec(uint64_t guest_addr, struct timespec *host);
int host_to_linux_timespec(struct timespec *host, uint64_t guest_addr);
int linux_to_host_timeval(uint64_t guest_addr, struct timeval *host);
int host_to_linux_timeval(struct timeval *host, uint64_t guest_addr);

// stat family
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    // ... Linux stat layout
} LinuxStat;

int linux_to_host_stat(uint64_t guest_addr, struct stat *host);
int host_to_linux_stat(struct stat *host, uint64_t guest_addr);

// sigaction
int linux_to_host_sigaction(uint64_t guest_addr, struct sigaction *host);
int host_to_linux_sigaction(struct sigaction *host, uint64_t guest_addr);

// sockaddr
int linux_to_host_sockaddr(uint64_t guest_addr, socklen_t len,
                           struct sockaddr_storage *host);
int host_to_linux_sockaddr(struct sockaddr_storage *host,
                           uint64_t guest_addr, socklen_t *len);

// rlimit
int linux_to_host_rlimit(uint64_t guest_addr, struct rlimit *host);
int host_to_linux_rlimit(struct rlimit *host, uint64_t guest_addr);

// iovec scatter-gather
int linux_to_host_iovec(CpuThread *cpu, uint64_t iov_guest,
                        int iovcnt, struct iovec **host_iov);
void free_host_iovec(struct iovec *iov);
```

**Implementation Notes**:
- Struct layouts defined explicitly (no assumptions)
- Field-by-field copying with size/alignment checks
- Endianness handled (though AArch64 is always LE)

**Dependencies**: `syscall_bridge.h`

**Build Order**: 5

---

### 3.5 Platform Layer

**File: `src/platform/ios_main.m`** (500 lines)

**Purpose**: iOS app entry point and setup

**Key Functions**:
```c
// App lifecycle
int main(int argc, char *argv[]);
void ios_setup(void);
void ios_guest_vm_init(void);

// Rootfs download and setup
void download_rootfs_if_needed(void);
void extract_rootfs(const char *tar_path, const char *dest);

// Guest VM startup
void start_guest_vm(const char *root_path);
```

**Implementation Notes**:
- Downloads aarch64 Alpine rootfs on first launch
- Sets up guest memory arena
- Creates initial guest thread

**Dependencies**: All other layers

**Build Order**: 6 (last)

---

**File: `src/platform/vdso.c`** (400 lines)

**Purpose**: Virtual DSO for fast syscalls

**Key Functions**:
```c
// VDSO page setup
void vdso_init(CpuThread *cpu);
void *vdso_get_page(void);

// VDSO implementations
int __vdso_clock_gettime(clockid_t clk, struct timespec *ts);
int __vdso_gettimeofday(struct timeval *tv, void *tz);
long __vdso_getcpu(unsigned *cpu, unsigned *node);
```

**Implementation Notes**:
- Single page mapped at fixed guest address
- Implements clock_gettime, gettimeofday, getcpu
- Avoids full syscall overhead for hot calls

**Dependencies**: `core.h`

**Build Order**: 4

---

## 4. Package Build Order with Dependencies

### 4.1 Build Phases

```
Phase 0: Code Generation
├── gadget_gen.py
│   ├── gadget_templates.json (input)
│   ├── gadget_bank.S (output)
│   └── gadget_table.c (output)
└── No dependencies

Phase 1: Headers
├── include/tcti/core.h
├── include/tcti/syscall_bridge.h
├── include/tcti/guest_memory.h
└── include/tcti/gadget_bank.h

Phase 2: Core Infrastructure
├── src/core/decoder.c
│   └── Depends: core.h
├── src/gadgets/gadget_bank_aarch64.S
│   └── Depends: gadget_bank.h (generated)
└── libcore.a

Phase 3: Execution Engine
├── src/core/tcti_exec.c
│   ├── Depends: core.h, decoder.h, gadget_bank.h
│   └── Links: libcore.a
└── libexec.a

Phase 4: Memory Management
├── src/memory/guest_mm.c
├── src/memory/tlb.c
│   └── Depends: core.h, guest_memory.h
└── libmemory.a

Phase 5: Syscall Bridge
├── src/syscall/syscall_dispatch.c
├── src/syscall/syscall_file.c
├── src/syscall/syscall_mmap.c
├── src/syscall/syscall_thread.c
├── src/syscall/syscall_signal.c
├── src/syscall/struct_marshal.c
│   └── Depends: core.h, syscall_bridge.h, guest_memory.h
└── libsyscall.a

Phase 6: Platform & VDSO
├── src/platform/vdso.c
│   └── Depends: core.h
└── libplatform.a

Phase 7: Final Link
├── src/platform/ios_main.m
│   ├── Depends: All libraries
│   └── Links: libcore.a, libexec.a, libmemory.a, 
│              libsyscall.a, libplatform.a, 
│              gadget_bank_aarch64.o
└── ish (executable)
```

### 4.2 Meson Build Configuration

**File: `meson.build`** (main build file)

```python
project('ish-tcti', 'c', 'cpp', 'objc',
    version: '1.0.0',
    default_options: [
        'warning_level=3',
        'c_std=gnu11',
        'cpp_std=c++17',
        'optimization=3',
        'b_lto=true',
    ]
)

# Compiler flags for iOS
if host_machine.system() == 'darwin'
    add_project_arguments(
        '-arch', 'arm64',
        '-isysroot', '/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk',
        '-mios-version-min=14.0',
        language: ['c', 'cpp', 'objc']
    )
    add_project_link_arguments(
        '-arch', 'arm64',
        '-framework', 'Foundation',
        '-framework', 'UIKit',
        language: ['c', 'cpp', 'objc']
    )
endif

# Code generation step
python3 = find_program('python3')
gadget_templates = files('src/gadgets/gadget_templates.json')

gadget_bank = custom_target('gadget_bank',
    input: gadget_templates,
    output: ['gadget_bank.S', 'gadget_table.c'],
    command: [python3, 'src/gadgets/gadget_gen.py', '@INPUT@', '@OUTPUT@']
)

# Include directories
inc = include_directories('include', 'include/tcti')

# Phase 2: Core
core_sources = files(
    'src/core/decoder.c',
    'src/core/tcti_exec.c',
)

gadget_obj = static_library('gadget_obj',
    gadget_bank[0],  # gadget_bank.S
    include_directories: inc
)

core_lib = static_library('core',
    core_sources,
    include_directories: inc,
    link_with: gadget_obj
)

# Phase 4: Memory
memory_lib = static_library('memory',
    'src/memory/guest_mm.c',
    'src/memory/tlb.c',
    include_directories: inc,
    link_with: core_lib
)

# Phase 5: Syscall
syscall_lib = static_library('syscall',
    'src/syscall/syscall_dispatch.c',
    'src/syscall/syscall_file.c',
    'src/syscall/syscall_mmap.c',
    'src/syscall/syscall_thread.c',
    'src/syscall/syscall_signal.c',
    'src/syscall/struct_marshal.c',
    include_directories: inc,
    link_with: [core_lib, memory_lib]
)

# Phase 6: Platform
platform_lib = static_library('platform',
    'src/platform/vdso.c',
    include_directories: inc,
    link_with: [core_lib, memory_lib]
)

# Phase 7: Main executable
ish_exe = executable('ish',
    'src/platform/ios_main.m',
    include_directories: inc,
    link_with: [core_lib, memory_lib, syscall_lib, platform_lib, gadget_obj],
    install: true
)

# Tests
test_decoder = executable('test_decoder',
    'tests/test_decoder.c',
    include_directories: inc,
    link_with: core_lib
)
test('decoder', test_decoder)

test_syscall = executable('test_syscall',
    'tests/test_syscall.c',
    include_directories: inc,
    link_with: [core_lib, syscall_lib]
)
test('syscall', test_syscall)

test_memory = executable('test_memory',
    'tests/test_memory.c',
    include_directories: inc,
    link_with: [core_lib, memory_lib]
)
test('memory', test_memory)
```

---

## 5. Testing Strategy

### 5.1 Unit Tests

**File: `tests/test_decoder.c`** (800 lines)

```c
// Test instruction decoding accuracy
void test_add_reg(void) {
    uint32_t insn = 0x0b020001;  // add x1, x0, x2
    IrInst ir;
    int ret = decode_instruction(insn, &ir);
    assert(ret == 0);
    assert(ir.op == OP_ADD);
    assert(ir.rd == 1);
    assert(ir.rn == 0);
    assert(ir.rm == 2);
}

void test_memory_ops(void) {
    // Test load/store decoding
    // Test offset calculations
    // Test addressing modes
}

void test_branch_decode(void) {
    // Test B, BL, CBZ, CBNZ decoding
    // Test target PC calculations
}

int main(void) {
    test_add_reg();
    test_memory_ops();
    test_branch_decode();
    printf("All decoder tests passed!\n");
    return 0;
}
```

**File: `tests/test_syscall.c`** (1,000 lines)

```c
// Test syscall marshalling
void test_timespec_marshal(void) {
    // Test Linux ↔ Darwin timespec conversion
}

void test_stat_marshal(void) {
    // Test stat struct layout conversion
}

void test_errno_conversion(void) {
    // Test errno value mapping
}

void test_futex_basic(void) {
    // Test futex wait/wake
}

int main(void) {
    // Run tests
}
```

**File: `tests/test_memory.c`** (800 lines)

```c
// Test guest memory management
void test_vma_tree(void) {
    // Test insertion, lookup, removal
}

void test_tlb_basic(void) {
    // Test TLB hit/miss
}

void test_page_fault(void) {
    // Test page fault handling
}

void test_mmap_emulation(void) {
    // Test mmap/munmap/mprotect
}

int main(void) {
    // Run tests
}
```

### 5.2 Integration Tests

**File: `tests/integration/test_minimal.c`** (600 lines)

```c
// Minimal guest program execution
// Tests: TB lookup, TCTI execution, syscall exit
void test_minimal_exit(void) {
    // Create minimal guest program:
    // mov x8, #93      (SYS_exit)
    // mov x0, #0       (exit code)
    // svc #0
    
    // Execute and verify clean exit
}

void test_minimal_hello(void) {
    // Test write syscall
    // Verify output appears
}
```

**File: `tests/integration/test_signals.c`** (800 lines)

```c
// Signal delivery tests
void test_sigsegv_delivery(void) {
    // Set up SIGSEGV handler
    // Trigger segfault
    // Verify handler called with correct info
}

void test_alarm_signal(void) {
    // Set up SIGALRM handler
    // Set alarm
    // Verify handler called
}
```

**File: `tests/integration/test_threads.c`** (1,000 lines)

```c
// Thread creation and synchronization
void test_thread_create(void) {
    // Test clone with CLONE_VM
    // Verify thread runs and exits
}

void test_futex_wait_wake(void) {
    // Test basic futex operations
}

void test_mutex_emulation(void) {
    // Test pthread_mutex_t emulation
}
```

### 5.3 Real Workload Tests

**File: `tests/workloads/run_busybox.sh`**

```bash
#!/bin/bash
# Test with real BusyBox binary

BUSYBOX_URL="https://busybox.net/downloads/binaries/1.35.0-arm64-linux-musl/busybox"

download_busybox() {
    wget -q "$BUSYBOX_URL" -O busybox
    chmod +x busybox
}

test_busybox_ls() {
    ./ish busybox ls -la
}

test_busybox_echo() {
    ./ish busybox echo "Hello from TCTI"
}

test_busybox_shell() {
    # Run shell commands
    echo 'echo $SHELL; exit' | ./ish busybox sh
}

test_busybox_tar() {
    # Test file I/O
    ./ish busybox tar -czf test.tar.gz /etc
}

main() {
    download_busybox
    test_busybox_ls
    test_busybox_echo
    test_busybox_shell
    test_busybox_tar
}

main "$@"
```

**File: `tests/workloads/run_alpine.sh`**

```bash
#!/bin/bash
# Test with Alpine minirootfs

ALPINE_VERSION="3.23.3"
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/aarch64/alpine-minirootfs-${ALPINE_VERSION}-aarch64.tar.gz"

setup_alpine() {
    mkdir -p alpine-root
    wget -q "$ALPINE_URL" -O alpine.tar.gz
    tar -xzf alpine.tar.gz -C alpine-root
}

test_apk_install() {
    # Test package manager
    ./ish /bin/sh -c "apk add --no-cache curl"
}

test_shell_loop() {
    # Performance test
    time ./ish /bin/sh -c "for i in \$(seq 1 1000); do echo \$i; done"
}

main() {
    setup_alpine
    test_apk_install
    test_shell_loop
}

main "$@"
```

### 5.4 Performance Benchmarks

**File: `tests/benchmark/bench_tcti.c`** (1,000 lines)

```c
// Performance counters
typedef struct {
    uint64_t instructions_executed;
    uint64_t tb_l0_hits;
    uint64_t tb_l1_hits;
    uint64_t tb_compiles;
    uint64_t chain_hits;
    uint64_t helper_calls;
    uint64_t tlb_hits;
    uint64_t tlb_misses;
    uint64_t syscall_count;
    uint64_t signals_delivered;
    double wall_time_ns;
} PerfCounters;

void benchmark_fibonacci(void) {
    // Recursive fibonacci
    // Measures: call/ret, arithmetic, TB chaining
}

void benchmark_malloc(void) {
    // malloc/free stress test
    // Measures: brk, mmap, syscall overhead
}

void_benchmark_memcpy(void) {
    // Memory copy benchmark
    // Measures: load/store, TLB efficiency
}

void benchmark_syscall(void) {
    // getpid in tight loop
    // Measures: syscall enter/exit overhead
}

void report_performance(PerfCounters *c) {
    printf("=== Performance Report ===\n");
    printf("Instructions:     %lu\n", c->instructions_executed);
    printf("L0 Hit Rate:      %.2f%%\n", 
           100.0 * c->tb_l0_hits / (c->tb_l0_hits + c->tb_l1_hits + c->tb_compiles));
    printf("Chain Hit Rate:   %.2f%%\n",
           100.0 * c->chain_hits / c->instructions_executed);
    printf("TLB Hit Rate:     %.2f%%\n",
           100.0 * c->tlb_hits / (c->tlb_hits + c->tlb_misses));
    printf("IPC (est):        %.2f\n",
           (double)c->instructions_executed / (c->wall_time_ns / 1e9));
}
```

### 5.5 CI/CD Testing

**File: `.github/workflows/test.yml`**

```yaml
name: Test Suite

on: [push, pull_request]

jobs:
  test-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          brew install meson ninja python3
          pip3 install -r requirements.txt
      
      - name: Generate gadget bank
        run: python3 src/gadgets/gadget_gen.py src/gadgets/gadget_templates.json src/gadgets/gadget_bank.S src/gadgets/gadget_table.c
      
      - name: Build
        run: |
          meson setup build
          ninja -C build
      
      - name: Run unit tests
        run: meson test -C build --verbose
      
      - name: Run integration tests
        run: |
          ./tests/integration/test_minimal
          ./tests/integration/test_signals
          ./tests/integration/test_threads
      
      - name: Run workload tests
        run: |
          ./tests/workloads/run_busybox.sh
          ./tests/workloads/run_alpine.sh
      
      - name: Run benchmarks
        run: ./tests/benchmark/bench_tcti
      
      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: benchmark-results
          path: benchmark_results.json
```

### 5.6 Test Acceptance Criteria

**Per-PR Acceptance Tests:**
1. All unit tests pass (decoder, syscall, memory)
2. Minimal integration tests pass (exit, hello, signals)
3. No regressions in L0 cache hit rate
4. No regressions in allocation counts

**Release Acceptance Tests:**
1. Full integration test suite passes
2. BusyBox test suite passes
3. Alpine package manager works
4. Thread creation and futex tests pass
5. Self-modifying code tests pass
6. Signal delivery tests pass
7. Memory stress tests pass
8. Benchmarks show improvement or parity

**Metrics Thresholds:**
- L0 Cache Hit Rate: > 95%
- TLB Hit Rate: > 98%
- Helper Call Rate: < 0.1% of instructions
- Allocation Rate: 0 in steady-state
- Chain Success Rate: > 90%

---

## 6. Milestone Schedule

### Milestone 1: Core Execution (Weeks 1-4)
**Goal**: Single-threaded execution, minimal syscalls

**Deliverables**:
- [ ] Decoder for common instructions (add/sub/mov/branch)
- [ ] TCTI gadget bank (arithmetic, logic, move)
- [ ] L0/L1 TB cache
- [ ] Persistent execution context
- [ ] Basic TB chaining
- [ ] exit, write syscalls
- [ ] Hello World runs

**Success Metrics**:
- Zero allocations in steady-state
- L0 hit rate visible
- Can run: `echo "Hello"`

### Milestone 2: Memory & Signals (Weeks 5-8)
**Goal**: Complete memory management, signal handling

**Deliverables**:
- [ ] Guest VMA implementation
- [ ] mmap/munmap/mprotect
- [ ] Software TLB
- [ ] Page fault handling
- [ ] Signal queue and delivery
- [ ] Sticky compiled-page bitmap
- [ ] Basic shell runs

**Success Metrics**:
- Can run: shell loops
- Page invalidation correct
- Signal delivery works

### Milestone 3: Threads & Futex (Weeks 9-12)
**Goal**: Multi-threading support

**Deliverables**:
- [ ] clone() implementation
- [ ] Per-thread L0 caches
- [ ] Futex wait/wake
- [ ] Thread exit/join
- [ ] Epoch reclamation
- [ ] Parallel workloads run

**Success Metrics**:
- Can run: `make -j4`
- No jetsam_lock contention
- Thread-safe invalidation

### Milestone 4: Optimization (Weeks 13-16)
**Goal**: Production performance

**Deliverables**:
- [ ] Lockless chain patch fast reject
- [ ] 2-way return cache
- [ ] VDSO shortcuts
- [ ] FD shadow table
- [ ] Block allocator
- [ ] Optional 2-way TLB

**Success Metrics**:
- > 50% of native speed for compute
- < 10% overhead for I/O
- Memory usage bounded

---

## 7. Risk Mitigation

### Technical Risks

**Risk**: TCTI gadget bank too large for iOS app size limits
**Mitigation**: Profile-driven specialization, only hot opcodes

**Risk**: Darwin/Linux ABI differences break compatibility
**Mitigation**: Extensive struct marshalling tests, syscall whitelist

**Risk**: Memory model differences cause subtle bugs
**Mitigation**: Conservative atomics, thorough threading tests

**Risk**: Performance not competitive with JIT
**Mitigation**: Aggressive optimization, measure everything

### Schedule Risks

**Risk**: Decoder complexity underestimated
**Mitigation**: Start with subset, expand incrementally

**Risk**: Syscall bridge takes longer than expected
**Mitigation**: Prioritize Class A/B syscalls, defer C/D

---

## Appendix A: Syscall Support Matrix

| Syscall | Class | Status | Notes |
|---------|-------|--------|-------|
| read | A | Required | Direct host call |
| write | A | Required | Direct host call |
| close | A | Required | FD shadow table |
| lseek | A | Required | Direct host call |
| mmap | B | Required | VMA + host mmap |
| munmap | B | Required | VMA tracking |
| mprotect | B | Required | VMA update |
| brk | C | Required | Heap emulation |
| exit | C | Required | Thread exit |
| exit_group | C | Required | Process exit |
| getpid | A | Required | Cached value |
| gettid | A | Required | Cached value |
| clone | C | Required | Thread creation |
| futex | C | Required | Hash table + condvars |
| openat | B | Required | Flags translation |
| newfstatat | B | Required | Struct marshalling |
| rt_sigaction | B | Required | Signal table |
| rt_sigprocmask | B | Required | Mask tracking |
| rt_sigreturn | C | Required | Context restore |
| clock_gettime | A | VDSO | VDSO preferred |
| nanosleep | A | Required | Direct host call |
| ioctl | B | Deferred | Whitelist approach |
| prlimit64 | B | Deferred | rlimit translation |
| set_tid_address | C | Required | Thread metadata |
| uname | A | Required | Static values |

---

## Appendix B: Performance Targets

### Microbenchmarks (vs Native Linux on same hardware)

| Workload | Target | Notes |
|----------|--------|-------|
| Integer arithmetic loop | 80% | Pure gadget chains |
| Memory copy (aligned) | 60% | TLB-dependent |
| System call overhead | 5x | Bridge cost |
| Thread create/exit | 10x | Host pthread |
| Context switch | 3x | TB cache reload |

### Macrobenchmarks

| Workload | Target |
|----------|--------|
| Build Linux kernel | < 10x native |
| Run shell scripts | < 5x native |
| Package install | < 3x native |
| File I/O heavy | < 2x native |

---

*Document Version: 1.0*
*Last Updated: 2026-03-21*
*Author: Implementation Team*
