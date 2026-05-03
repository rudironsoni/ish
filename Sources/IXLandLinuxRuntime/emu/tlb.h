#ifndef TLB_H
#define TLB_H

#include <IXLandLinuxRuntime/emu/mmu.h>
#include <IXLandLinuxRuntime/util/debug.h>
#include <string.h>

/*
 * TLB with generation-based validity contract.
 *
 * Each TLB entry carries the translation generation at install time.
 * On lookup, the entry is valid ONLY if:
 *   1. The page tag matches
 *   2. The generation matches the current mmu->generation
 *   3. The permission check passes (writable vs readable)
 *
 * This replaces the old model where entries were valid based on page tag
 * alone, which allowed stale host pointers to be used after unmap/remap.
 *
 * INVARIANTS:
 * - TLB entries MUST NOT be installed after a generation change.
 * - tlb_handle_miss snapshots generation BEFORE translation, translates,
 *   then checks generation AFTER. If changed, it flushes and retries.
 * - This guarantees no stale pointer is ever cached.
 */

struct tlb_entry {
    page_t page;                 /* guest page tag */
    page_t page_if_writable;     /* guest page tag (only valid for writes) */
    uintptr_t data_minus_addr;   /* host_base - guest_page_addr delta */
    mem_generation_t generation; /* translation generation at install time */
};

#define TLB_BITS 10
#define TLB_SIZE (1 << TLB_BITS)

struct fiber_exec_ctx;

struct tlb {
    struct mmu *mmu;
    page_t dirty_page;
    mem_generation_t generation; /* snapshot of mmu->generation at last flush */
    addr_t segfault_addr;
    struct tlb_entry entries[TLB_SIZE];
    struct fiber_exec_ctx *stats_ctx;
};

#define TLB_INDEX(addr)                                                                            \
    ((((addr >> PAGE_BITS) & (TLB_SIZE - 1)) ^ (addr >> (PAGE_BITS + TLB_BITS))) & (TLB_SIZE - 1))
#define TLB_PAGE(addr) ((addr) & 0xFFFFFFFFFFFFF000ULL)
#define TLB_PAGE_EMPTY 1

void tlb_refresh(struct tlb *tlb, struct mmu *mmu);
void tlb_free(struct tlb *tlb);
void tlb_flush(struct tlb *tlb);
void *tlb_handle_miss(struct tlb *tlb, addr_t addr, int type);

forceinline __no_instrument void *__tlb_read_ptr(struct tlb *tlb, addr_t addr)
{
    if (tlb == NULL || tlb->mmu == NULL)
        return NULL;
    struct tlb_entry entry = tlb->entries[TLB_INDEX(addr)];
    if (entry.page != 0 && entry.page != TLB_PAGE_EMPTY && entry.page == TLB_PAGE(addr) &&
        entry.generation == tlb->mmu->generation) {
        void *address = (void *)(entry.data_minus_addr + addr);
        posit(address != NULL);
        return address;
    }
    return tlb_handle_miss(tlb, addr, MEM_READ);
}

bool __tlb_read_cross_page(struct tlb *tlb, addr_t addr, char *out, unsigned size);

forceinline __no_instrument bool tlb_read(struct tlb *tlb, addr_t addr, void *out, unsigned size)
{
    if (PGOFFSET(addr) > PAGE_SIZE - size)
        return __tlb_read_cross_page(tlb, addr, out, size);
    void *ptr = __tlb_read_ptr(tlb, addr);
    if (ptr == NULL)
        return false;
    memcpy(out, ptr, size);
    return true;
}

forceinline __no_instrument void *__tlb_write_ptr(struct tlb *tlb, addr_t addr)
{
    if (tlb == NULL || tlb->mmu == NULL)
        return NULL;
    struct tlb_entry entry = tlb->entries[TLB_INDEX(addr)];
    if (entry.page_if_writable != 0 && entry.page_if_writable != TLB_PAGE_EMPTY &&
        entry.page_if_writable == TLB_PAGE(addr) && entry.generation == tlb->mmu->generation) {
        tlb->dirty_page = TLB_PAGE(addr);
        void *address = (void *)(entry.data_minus_addr + addr);
        posit(address != NULL);
        return address;
    }
    return tlb_handle_miss(tlb, addr, MEM_WRITE);
}

bool __tlb_write_cross_page(struct tlb *tlb, addr_t addr, const char *value, unsigned size);

forceinline __no_instrument bool tlb_write(struct tlb *tlb, addr_t addr, const void *value,
                                           unsigned size)
{
    if (PGOFFSET(addr) > PAGE_SIZE - size)
        return __tlb_write_cross_page(tlb, addr, value, size);
    void *ptr = __tlb_write_ptr(tlb, addr);
    if (ptr == NULL)
        return false;
    memcpy(ptr, value, size);
    return true;
}

#endif
