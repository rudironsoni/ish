#import <IXLandLinuxRuntime/emu/aarch64/tls.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/musl_thread.h>

enum {
    A64_MUSL_DT_JOINABLE = 2,
};

a64_musl_thread_layout_t a64_musl_thread_layout_make(addr_t thread_pointer_offset)
{
    if (thread_pointer_offset == A64_MUSL_LEGACY_THREAD_POINTER_OFFSET) {
        return (a64_musl_thread_layout_t){
            .thread_pointer_offset = thread_pointer_offset,
            .pthread_canary_offset = A64_MUSL_PTHREAD_CANARY_TAIL_OFFSET,
            .pthread_dtv_offset = A64_MUSL_PTHREAD_DTV_TAIL_OFFSET,
        };
    }

    return (a64_musl_thread_layout_t){
        .thread_pointer_offset = thread_pointer_offset,
        .pthread_canary_offset = A64_MUSL_LEGACY_PTHREAD_CANARY_OFFSET,
        .pthread_dtv_offset = A64_MUSL_LEGACY_PTHREAD_DTV_OFFSET,
    };
}

int a64_bootstrap_initial_musl_thread(struct cpu_state *cpu,
                                      const a64_musl_thread_layout_t *layout, addr_t pthread_base,
                                      addr_t dtv_base, uint64_t dtv_slot_count, uint64_t canary)
{
    if (layout == NULL)
        return _EINVAL;

    addr_t thread_pointer = pthread_base + layout->thread_pointer_offset;
    addr_t robust_head = pthread_base + A64_MUSL_PTHREAD_ROBUST_HEAD_OFFSET;
    pid_t_ tid = current ? current->pid : 0;
    int detach_state = A64_MUSL_DT_JOINABLE;
    uint64_t zero64 = 0;
    int zero32 = 0;
    int killlock = 0;

    // AArch64 musl uses TLS_ABOVE_TP, but the exact TP delta is part of the
    // guest libc ABI and differs across musl generations. We seed only the
    // singleton fields that early guest startup dereferences before libc
    // finishes its normal runtime initialization, while placing canary/dtv at
    // the layout-selected tail slots used by the actual interpreter.
    if (user_put(pthread_base + A64_MUSL_PTHREAD_SELF_OFFSET, pthread_base) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_PREV_OFFSET, pthread_base) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_NEXT_OFFSET, pthread_base) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_SYSINFO_OFFSET, zero64) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_TID_OFFSET, tid) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_ERRNO_OFFSET, zero32) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_DETACH_OFFSET, detach_state) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_ROBUST_HEAD_OFFSET, robust_head) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_H_ERRNO_OFFSET, zero32) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_TIMER_ID_OFFSET, zero32) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_LOCALE_OFFSET, zero64) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_KILLLOCK_OFFSET, killlock) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_DLERROR_BUF_OFFSET, zero64) ||
        user_put(pthread_base + A64_MUSL_PTHREAD_STDIO_LOCKS_OFFSET, zero64) ||
        user_put(pthread_base + layout->pthread_canary_offset, canary) ||
        user_put(pthread_base + layout->pthread_dtv_offset, dtv_base) ||
        user_put(dtv_base, dtv_slot_count)) {
        return _EFAULT;
    }

    return a64_setup_tls_area(cpu, thread_pointer);
}
