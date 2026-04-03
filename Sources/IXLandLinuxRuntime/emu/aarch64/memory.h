#ifndef AARCH64_MEMORY_H
#define AARCH64_MEMORY_H

/*
 * aarch64 memory access with TLB translation
 * Used by TCTI gadgets for load/store operations
 */

#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>

// Memory operation flags
#define A64_MEM_READ  0
#define A64_MEM_WRITE 1
#define A64_MEM_EXEC  2

// Access sizes
#define A64_MEM_8BIT  0
#define A64_MEM_16BIT 1
#define A64_MEM_32BIT 2
#define A64_MEM_64BIT 3

// Result codes
#define A64_MEM_OK    0
#define A64_MEM_FAULT 1

struct tlb;

/*
 * Read from guest memory with TLB translation
 * Handles page faults by setting cpu->fault_addr
 */
int a64_guest_read8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t *val);
int a64_guest_read16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t *val);
int a64_guest_read32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t *val);
int a64_guest_read64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t *val);

/*
 * Write to guest memory with TLB translation
 */
int a64_guest_write8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t val);
int a64_guest_write16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t val);
int a64_guest_write32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t val);
int a64_guest_write64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t val);

/*
 * Generic access with size parameter
 */
int a64_guest_read(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, void *val, int size);
int a64_guest_write(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, const void *val, int size);

/*
 * Atomic operations (for ldxr/stxr pairs)
 */
int a64_guest_ldxr8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t *val);
int a64_guest_ldxr16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t *val);
int a64_guest_ldxr32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t *val);
int a64_guest_ldxr64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t *val);

int a64_guest_stxr8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t val, int *success);
int a64_guest_stxr16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t val, int *success);
int a64_guest_stxr32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t val, int *success);
int a64_guest_stxr64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t val, int *success);

/*
 * Exclusive monitor operations
 * aarch64 uses exclusive monitors for atomic operations
 */
void a64_clear_exclusive(struct cpu_state *cpu);
int a64_check_exclusive(struct cpu_state *cpu, uint64_t addr, int size);
void a64_set_exclusive(struct cpu_state *cpu, uint64_t addr, int size);

/*
 * TLB management for aarch64
 */
void a64_tlb_init(struct tlb *tlb);
void a64_tlb_flush(struct tlb *tlb);
void a64_tlb_flush_page(struct tlb *tlb, uint64_t addr);

/*
 * Get host address for a guest address (if mapped)
 * Returns NULL if page not mapped
 */
void *a64_guest_to_host(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, int write);

/*
 * Page fault handling
 */
void a64_handle_page_fault(struct cpu_state *cpu, uint64_t addr, int write);

#endif
