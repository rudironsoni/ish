#ifndef UTIL_MISC_H
#define UTIL_MISC_H

#include <assert.h>
#include <ixland/linux_types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include <sys/types.h>

#define glue(a, b)        _glue(a, b)
#define _glue(a, b)       a##b
#define glue3(a, b, c)    glue(a, glue(b, c))
#define glue4(a, b, c, d) glue(a, glue3(b, c, d))
#define str(x)            _str(x)
#define _str(x)           #x

#define is_gcc(version) (__GNUC__ >= version)

#if !defined(__has_attribute)
#define has_attribute(x) 0
#else
#define has_attribute __has_attribute
#endif

#if !defined(__has_feature)
#define has_feature(x) 0
#else
#define has_feature __has_feature
#endif

#define bitfield    unsigned int
#define forceinline static inline __attribute__((always_inline))

#if defined(NDEBUG)
#define posit __builtin_assume
#else
#define posit assert
#endif

#define must_check __attribute__((warn_unused_result))

#define unlikely(x) __builtin_expect((x), 0)

#define typecheck(type, x)                                                                         \
    ({                                                                                             \
        type _x = x;                                                                               \
        x;                                                                                         \
    })

#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#if has_attribute(fallthrough)
#define ish_fallthrough __attribute__((fallthrough))
#else
#define ish_fallthrough
#endif

#if has_attribute(no_sanitize)
#define __no_instrument_msan
#if defined(__has_feature)
#if has_feature(memory_sanitizer)
#undef __no_instrument_msan
#define __no_instrument_msan __attribute__((no_sanitize("memory")))
#endif
#endif
#define __no_instrument                                                                            \
    __attribute__((no_sanitize("address", "thread", "undefined", "leak"))) __no_instrument_msan
#else
#define __no_instrument
#endif

#if has_attribute(nonstring)
#define __strncpy_safe __attribute__((nonstring))
#else
#define __strncpy_safe
#endif

#define zero_init(type) ((type[1]){}[0])

#define pun(type, x)                                                                               \
    (((union {                                                                                     \
         typeof(x) _;                                                                              \
         type a;                                                                                   \
     })(x))                                                                                        \
         .a)

#define UNUSED(x) UNUSED_##x __attribute__((unused))
static inline void __use(int dummy __attribute__((unused)), ...) {}
#define use(...) __use(0, ##__VA_ARGS__)

#define array_size(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef int64_t sqword_t;
typedef uint64_t qword_t;
typedef uint32_t dword_t;
typedef int32_t sdword_t;
typedef uint16_t word_t;
typedef uint8_t byte_t;
typedef uint64_t uint_t;
typedef int64_t int_t;

#define uint(size) glue3(uint, size, _t)
#define sint(size) glue3(int, size, _t)

#define ERR_PTR(err) ((void *)(intptr_t)(err))
#define PTR_ERR(ptr) ((intptr_t)(ptr))
#define IS_ERR(ptr)  ((uintptr_t)(ptr) > (uintptr_t)-0xfff)

#endif
