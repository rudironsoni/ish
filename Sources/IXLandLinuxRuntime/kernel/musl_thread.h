#ifndef IXLAND_KERNEL_MUSL_THREAD_H
#define IXLAND_KERNEL_MUSL_THREAD_H

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/kernel/task.h>

#define A64_MUSL_THREAD_POINTER_OFFSET     ((addr_t)184)
#define A64_MUSL_LEGACY_THREAD_POINTER_OFFSET ((addr_t)200)
#define A64_MUSL_PTHREAD_SELF_OFFSET       ((addr_t)0x00)
#define A64_MUSL_PTHREAD_PREV_OFFSET       ((addr_t)0x08)
#define A64_MUSL_PTHREAD_NEXT_OFFSET       ((addr_t)0x10)
#define A64_MUSL_PTHREAD_SYSINFO_OFFSET    ((addr_t)0x18)
#define A64_MUSL_PTHREAD_TID_OFFSET        ((addr_t)0x20)
#define A64_MUSL_PTHREAD_ERRNO_OFFSET      ((addr_t)0x24)
#define A64_MUSL_PTHREAD_DETACH_OFFSET     ((addr_t)0x28)
#define A64_MUSL_PTHREAD_ROBUST_HEAD_OFFSET ((addr_t)0x78)
#define A64_MUSL_PTHREAD_H_ERRNO_OFFSET    ((addr_t)0x90)
#define A64_MUSL_PTHREAD_TIMER_ID_OFFSET   ((addr_t)0x94)
#define A64_MUSL_PTHREAD_LOCALE_OFFSET     ((addr_t)0x98)
#define A64_MUSL_PTHREAD_KILLLOCK_OFFSET   ((addr_t)0xa0)
#define A64_MUSL_PTHREAD_DLERROR_BUF_OFFSET ((addr_t)0xa8)
#define A64_MUSL_PTHREAD_STDIO_LOCKS_OFFSET ((addr_t)0xb0)
#define A64_MUSL_PTHREAD_CANARY_TAIL_OFFSET ((addr_t)0xb8)
#define A64_MUSL_PTHREAD_DTV_TAIL_OFFSET    ((addr_t)0xc0)
#define A64_MUSL_LEGACY_PTHREAD_DTV_OFFSET  ((addr_t)0x08)
#define A64_MUSL_LEGACY_PTHREAD_CANARY_OFFSET ((addr_t)0x28)

typedef struct a64_musl_thread_layout {
    addr_t thread_pointer_offset;
    addr_t pthread_canary_offset;
    addr_t pthread_dtv_offset;
} a64_musl_thread_layout_t;

a64_musl_thread_layout_t a64_musl_thread_layout_make(addr_t thread_pointer_offset);

int a64_bootstrap_initial_musl_thread(struct cpu_state *cpu,
                                      const a64_musl_thread_layout_t *layout, addr_t pthread_base,
                                      addr_t dtv_base, uint64_t dtv_slot_count, uint64_t canary);

#endif
