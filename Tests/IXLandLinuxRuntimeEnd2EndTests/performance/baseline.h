/*
 * Performance Baseline Testing Framework
 *
 * Provides standardized test result collection and JSON export
 * for tracking performance across devices and optimization phases.
 */

#ifndef PERFORMANCE_BASELINE_H
#define PERFORMANCE_BASELINE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#import <IXLandLinuxRuntime/platform/ios/timing.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEST_NAME_MAX 128
#define DEVICE_NAME_MAX 64
#define MAX_TESTS 32

/*
 * Single test result structure
 */
typedef struct {
    // Device identification
    char device_name[DEVICE_NAME_MAX];
    char device_model[DEVICE_NAME_MAX];
    int cpu_cores;
    uint64_t memory_bytes;
    
    // Test metadata
    char test_name[TEST_NAME_MAX];
    uint64_t timestamp;
    char git_commit[41];  // SHA-1 + null
    
    // Timing results
    uint64_t total_ns;
    uint64_t iterations;
    double ns_per_op;
    double ops_per_sec;
    
    // iSH internal counters (from fiber_exec_ctx)
    uint64_t tb_compiles;
    uint64_t tb_l0_hits;
    uint64_t tb_l1_hits;
    double tb_hit_rate;
    
    uint64_t tlb_hits;
    uint64_t tlb_misses;
    double tlb_hit_rate;
    
    uint64_t chain_patches;
    uint64_t chain_success;
    double chain_success_rate;
    
    uint64_t ret_cache_hits;
    uint64_t ret_cache_misses;
    double ret_cache_hit_rate;
    
    // Memory stats
    uint64_t allocations;
    uint64_t bytes_allocated;
    uint64_t retired_blocks;
    
} TestResult;

/*
 * Test suite results collection
 */
typedef struct {
    char device_name[DEVICE_NAME_MAX];
    char device_model[DEVICE_NAME_MAX];
    int cpu_cores;
    uint64_t memory_bytes;
    uint64_t timestamp;
    char git_commit[41];
    
    TestResult tests[MAX_TESTS];
    int num_tests;
    
} TestSuite;

/*
 * Initialize test suite with device info
 */
void test_suite_init(TestSuite *suite);

/*
 * Add a test result to suite
 */
void test_suite_add(TestSuite *suite, const TestResult *result);

/*
 * Export test suite to JSON file
 * Returns 0 on success, -1 on error
 */
int test_suite_export_json(const TestSuite *suite, const char *path);

/*
 * Import test suite from JSON file
 * Returns 0 on success, -1 on error
 */
int test_suite_import_json(TestSuite *suite, const char *path);

/*
 * Compare current results to baseline
 * tolerance: fraction allowed (0.05 = 5% regression allowed)
 * Returns: number of regressions found
 */
int test_suite_compare(const TestSuite *current, const TestSuite *baseline, 
                       double tolerance, char *report, size_t report_size);

/*
 * Print test result to stdout
 */
void test_result_print(const TestResult *result);

/*
 * Print test suite summary to stdout
 */
void test_suite_print_summary(const TestSuite *suite);

/*
 * Get device info from system
 */
void get_device_info(char *name, size_t name_size,
                     char *model, size_t model_size,
                     int *cpu_cores, uint64_t *memory_bytes);

/*
 * Get current git commit hash
 */
void get_git_commit(char *commit, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PERFORMANCE_BASELINE_H */
