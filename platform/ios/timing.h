/*
 * iOS High-Resolution Timing Utilities
 *
 * Provides mach_absolute_time() based timing for performance benchmarking.
 * Resolution: ~41.67ns on modern iOS devices (24MHz timebase)
 */

#ifndef IOS_TIMING_H
#define IOS_TIMING_H

#include <mach/mach_time.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * iOS Timer Structure
 *
 * Stores start time and cached timebase info for conversion to nanoseconds.
 * The timebase is device-specific and converts absolute time units to nanoseconds.
 */
typedef struct {
    uint64_t start;
    mach_timebase_info_data_t timebase;
    bool initialized;
} IosTimer;

/*
 * Initialize timer (must be called before using)
 * Caches timebase info to avoid repeated syscalls
 */
static inline void ios_timer_init(IosTimer *t) {
    if (!t->initialized) {
        mach_timebase_info(&t->timebase);
        t->initialized = true;
    }
}

/*
 * Start timing
 * Records current mach_absolute_time()
 */
static inline void ios_timer_start(IosTimer *t) {
    t->start = mach_absolute_time();
}

/*
 * Get elapsed time in nanoseconds
 * Returns time since ios_timer_start() was called
 */
static inline uint64_t ios_timer_elapsed_ns(IosTimer *t) {
    uint64_t end = mach_absolute_time();
    return (end - t->start) * t->timebase.numer / t->timebase.denom;
}

/*
 * Get elapsed time in microseconds
 */
static inline uint64_t ios_timer_elapsed_us(IosTimer *t) {
    return ios_timer_elapsed_ns(t) / 1000;
}

/*
 * Get elapsed time in milliseconds
 */
static inline uint64_t ios_timer_elapsed_ms(IosTimer *t) {
    return ios_timer_elapsed_ns(t) / 1000000;
}

/*
 * Convenience: Start and return a new timer
 */
static inline IosTimer ios_timer_create(void) {
    IosTimer t = {0};
    ios_timer_init(&t);
    return t;
}

/*
 * Benchmark a function
 * Example:
 *   uint64_t ns = ios_bench(1000000, my_function);
 */
#define IOS_BENCH(iterations, code) ({ \
    IosTimer _t = ios_timer_create(); \
    ios_timer_start(&_t); \
    for (int _i = 0; _i < (iterations); _i++) { code; } \
    ios_timer_elapsed_ns(&_t); \
})

/*
 * Get device timing info
 * Returns resolution in nanoseconds
 */
static inline double ios_get_timer_resolution_ns(void) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    // mach_absolute_time units * numer / denom = nanoseconds
    // So resolution is denom / numer nanoseconds per unit
    return (double)tb.denom / (double)tb.numer;
}

#ifdef __cplusplus
}
#endif

#endif /* IOS_TIMING_H */
