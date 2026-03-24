/*
 * aarch64 memory access with TLB translation
 *
 * Bridges TCTI gadgets to iSH's existing TLB/MMU infrastructure.
 * Handles guest virtual address -> host address translation.
 */

#include "emu/aarch64/memory.h"
#include "emu/aarch64/cpu.h"
#include "emu/tlb.h"
#include <string.h>

// Read operations
int a64_guest_read8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t *val) {
    if (!tlb_read(tlb, addr, val, 1)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

int a64_guest_read16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t *val) {
    if (!tlb_read(tlb, addr, val, 2)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    // aarch64 is little-endian by default
    return A64_MEM_OK;
}

int a64_guest_read32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t *val) {
    if (!tlb_read(tlb, addr, val, 4)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

int a64_guest_read64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t *val) {
    if (!tlb_read(tlb, addr, val, 8)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

// Write operations
int a64_guest_write8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t val) {
    if (!tlb_write(tlb, addr, &val, 1)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

int a64_guest_write16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t val) {
    if (!tlb_write(tlb, addr, &val, 2)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

int a64_guest_write32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t val) {
    if (!tlb_write(tlb, addr, &val, 4)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

int a64_guest_write64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t val) {
    if (!tlb_write(tlb, addr, &val, 8)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

// Generic access with size parameter
int a64_guest_read(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, void *val, int size) {
    if (size != 1 && size != 2 && size != 4 && size != 8)
        return A64_MEM_FAULT;

    if (!tlb_read(tlb, addr, val, size)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

int a64_guest_write(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, const void *val, int size) {
    if (size != 1 && size != 2 && size != 4 && size != 8)
        return A64_MEM_FAULT;

    if (!tlb_write(tlb, addr, val, size)) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }
    return A64_MEM_OK;
}

// Atomic load-linked (ldxr) - sets exclusive monitor
int a64_guest_ldxr8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t *val) {
    int ret = a64_guest_read8(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        cpu->exclusive_addr = addr;
        cpu->exclusive_size = 1;
        cpu->exclusive_valid = 1;
    }
    return ret;
}

int a64_guest_ldxr16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t *val) {
    int ret = a64_guest_read16(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        cpu->exclusive_addr = addr;
        cpu->exclusive_size = 2;
        cpu->exclusive_valid = 1;
    }
    return ret;
}

int a64_guest_ldxr32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t *val) {
    int ret = a64_guest_read32(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        cpu->exclusive_addr = addr;
        cpu->exclusive_size = 4;
        cpu->exclusive_valid = 1;
    }
    return ret;
}

int a64_guest_ldxr64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t *val) {
    int ret = a64_guest_read64(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        cpu->exclusive_addr = addr;
        cpu->exclusive_size = 8;
        cpu->exclusive_valid = 1;
    }
    return ret;
}

// Atomic store-conditional (stxr) - succeeds only if exclusive monitor valid
int a64_guest_stxr8(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint8_t val, int *success) {
    if (!cpu->exclusive_valid || cpu->exclusive_addr != addr || cpu->exclusive_size != 1) {
        *success = 0;
        return A64_MEM_OK;  // Not a fault, just failed store
    }
    int ret = a64_guest_write8(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        *success = 1;
        cpu->exclusive_valid = 0;  // Clear monitor on successful store
    }
    return ret;
}

int a64_guest_stxr16(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint16_t val, int *success) {
    if (!cpu->exclusive_valid || cpu->exclusive_addr != addr || cpu->exclusive_size != 2) {
        *success = 0;
        return A64_MEM_OK;
    }
    int ret = a64_guest_write16(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        *success = 1;
        cpu->exclusive_valid = 0;
    }
    return ret;
}

int a64_guest_stxr32(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint32_t val, int *success) {
    if (!cpu->exclusive_valid || cpu->exclusive_addr != addr || cpu->exclusive_size != 4) {
        *success = 0;
        return A64_MEM_OK;
    }
    int ret = a64_guest_write32(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        *success = 1;
        cpu->exclusive_valid = 0;
    }
    return ret;
}

int a64_guest_stxr64(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, uint64_t val, int *success) {
    if (!cpu->exclusive_valid || cpu->exclusive_addr != addr || cpu->exclusive_size != 8) {
        *success = 0;
        return A64_MEM_OK;
    }
    int ret = a64_guest_write64(cpu, tlb, addr, val);
    if (ret == A64_MEM_OK) {
        *success = 1;
        cpu->exclusive_valid = 0;
    }
    return ret;
}

// Exclusive monitor operations
void a64_clear_exclusive(struct cpu_state *cpu) {
    cpu->exclusive_valid = 0;
}

int a64_check_exclusive(struct cpu_state *cpu, uint64_t addr, int size) {
    return cpu->exclusive_valid && cpu->exclusive_addr == addr && cpu->exclusive_size == size;
}

void a64_set_exclusive(struct cpu_state *cpu, uint64_t addr, int size) {
    cpu->exclusive_addr = addr;
    cpu->exclusive_size = size;
    cpu->exclusive_valid = 1;
}

// TLB management
void a64_tlb_init(struct tlb *tlb) {
    tlb_flush(tlb);
}

void a64_tlb_flush(struct tlb *tlb) {
    tlb_flush(tlb);
    a64_clear_exclusive(NULL);
}

void a64_tlb_flush_page(struct tlb *tlb, uint64_t addr) {
    // iSH's TLB doesn't have per-page flush, so flush all
    (void)addr;
    tlb_flush(tlb);
}

// Get host address for a guest address
void *a64_guest_to_host(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, int write) {
    (void)cpu;
    if (write) {
        return __tlb_write_ptr(tlb, addr);
    } else {
        return __tlb_read_ptr(tlb, addr);
    }
}

// Page fault handling
void a64_handle_page_fault(struct cpu_state *cpu, uint64_t addr, int write) {
    cpu->fault_addr = addr;
    cpu->fault_was_write = write;
}
