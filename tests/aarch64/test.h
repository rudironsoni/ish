/*
 * Test framework for aarch64 emulation tests
 * Based on QEMU/UTM TCG test patterns
 */

#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Simple test framework - returns number of failures */
typedef int (*test_func_t)(void);

#define TEST_START(name) printf("  TEST: %-40s ", name); fflush(stdout)
#define TEST_PASS() do { printf("PASS\n"); return 0; } while (0)
#define TEST_FAIL(msg) do { printf("FAIL: %s\n", msg); return 1; } while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s:%d: Expected 0x%lx, got 0x%lx\n", \
               __FILE__, __LINE__, (uint64_t)(b), (uint64_t)(a)); \
        return 1; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s:%d: Assertion failed: %s\n", \
               __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

/* Run a test and accumulate failures */
#define RUN_TEST(func) do { \
    total++; \
    failures += func(); \
} while (0)

/* Print summary */
#define PRINT_SUMMARY(name) do { \
    printf("\n%s: %d tests, %d failures\n", name, total, failures); \
    return failures; \
} while (0)

#endif /* TEST_H */
