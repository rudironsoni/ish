// VDSO for AArch64 guest
// Provides userspace implementations of time functions and signal return

#include <stdint.h>

typedef long time_t;
typedef int clockid_t;

// VDSO entry point - AArch64 doesn't use this like x86, but linker expects it
// Just returns 0
int __kernel_vsyscall(void)
{
    return 0;
}

// Signal return trampoline for rt_sigreturn
// This matches the kernel/aarch64/vdso.c trampoline
void __kernel_rt_sigreturn(void)
{
    __asm__ volatile("mov x8, #139\n\t" // __NR_rt_sigreturn
                     "svc #0\n\t");
}

// Legacy sigreturn - not used on AArch64 but linker expects symbol
void __kernel_sigreturn(void)
{
    __asm__ volatile("mov x8, #119\n\t" // __NR_sigreturn
                     "svc #0\n\t");
}

time_t __vdso_time(time_t *t)
{
    time_t result;
    // AArch64 syscall: time
    __asm__ volatile("mov x8, #13\n\t" // __NR_time
                     "mov x0, %1\n\t"  // t
                     "svc #0\n\t"      // syscall
                     "mov %w0, w0\n\t" // result
                     : "=r"(result)
                     : "r"(t)
                     : "x0", "x8", "memory");
    return result;
}

int __vdso_gettimeofday(void *timeval, void *timezone)
{
    int result;
    // AArch64 syscall: gettimeofday
    __asm__ volatile("mov x8, #78\n\t" // __NR_gettimeofday
                     "mov x0, %1\n\t"  // timeval
                     "mov x1, %2\n\t"  // timezone
                     "svc #0\n\t"      // syscall
                     "mov %w0, w0\n\t" // result
                     : "=r"(result)
                     : "r"(timeval), "r"(timezone)
                     : "x0", "x1", "x8", "memory");
    return result;
}

int __vdso_clock_gettime(clockid_t clock, void *timespec)
{
    uint32_t result;
    uint64_t clock64 = (uint64_t)(uint32_t)clock;
    // AArch64 syscall: clock_gettime
    __asm__ volatile("mov x8, #113\n\t" // __NR_clock_gettime
                     "mov x0, %1\n\t"   // clock
                     "mov x1, %2\n\t"   // timespec
                     "svc #0\n\t"       // syscall
                     "mov %w0, w0\n\t"  // result
                     : "=r"(result)
                     : "r"(clock64), "r"(timespec)
                     : "x0", "x1", "x8", "memory");
    return (int)result;
}
