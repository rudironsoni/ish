#ifndef EMU_CPU_MEM_H
#define EMU_CPU_MEM_H

#include "misc.h"

// Forward declaration for block cache ownership
struct a64_block_cache;

// top 20 bits of an address, i.e. address >> 12
typedef dword_t page_t;
#define BAD_PAGE 0x10000

#ifndef __KERNEL__
#define PAGE_BITS 12
#undef PAGE_SIZE // defined in system headers somewhere
#define PAGE_SIZE (1 << PAGE_BITS)
#define PAGE(addr) ((addr) >> PAGE_BITS)
#define PGOFFSET(addr) ((addr) & (PAGE_SIZE - 1))
typedef dword_t pages_t;
// bytes MUST be unsigned if you would like this to overflow to zero
#define PAGE_ROUND_UP(bytes) (PAGE((bytes) + PAGE_SIZE - 1))
#define MEM_PAGES (1 << 20) // at least on 32-bit
#endif

struct mmu {
    struct mmu_ops *ops;
    // Block cache for compiled TCTI blocks
    // Scoped to address space (MMU) for correctness - PC values are only
    // meaningful within a specific address space context
    struct a64_block_cache *block_cache;
    uint64_t changes;
};

#define MEM_READ 0
#define MEM_WRITE 1
#define MEM_WRITE_PTRACE 2

struct mmu_ops {
    // type is MEM_READ or MEM_WRITE
    void *(*translate)(struct mmu *mmu, addr_t addr, int type);
};

static inline void *mmu_translate(struct mmu *mmu, addr_t addr, int type) {
    return mmu->ops->translate(mmu, addr, type);
}

#endif