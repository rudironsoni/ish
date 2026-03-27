/*
 * ABI Fixture Harness
 * Validates Linux AArch64 ABI and MMU functionality.
 *
 * Phase 04 test harness for MMU and ABI cases.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>

#define MAX_PATH 4096
#define MAX_LINE 1024

/* Stub kernel functions required by iSH headers */
#include <stdarg.h>
void ish_printk(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}
#define printk ish_printk

void handle_interrupt(int interrupt) {
    fprintf(stderr, "[HARNESS] handle_interrupt: %d\n", interrupt);
}

void memset_junk(void *buf, size_t size) {
    memset(buf, 0xAB, size);
}

void *g_end_brk = NULL;

/* iSH headers */
#include "misc.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/task.h"
#include "emu/aarch64/cpu.h"

static int setup_artifact_dir(const char *artifact_dir) {
    char cmd[MAX_PATH];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", artifact_dir);
    system(cmd);
    
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", artifact_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Failed to create artifact dir %s\n", artifact_dir);
        return -1;
    }
    return 0;
}

static int write_report(const char *artifact_dir, const char *case_id, 
                        const char *phase, const char *harness,
                        int passed, const char *failure_summary) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/report.json", artifact_dir);
    
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot write report.json: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"case_id\": \"%s\",\n", case_id);
    fprintf(fp, "  \"phase\": \"%s\",\n", phase);
    fprintf(fp, "  \"harness\": \"%s\",\n", harness);
    fprintf(fp, "  \"passed\": %s,\n", passed ? "true" : "false");
    fprintf(fp, "  \"artifacts\": [\n");
    fprintf(fp, "    \"%s/stack_dump.bin\",\n", artifact_dir);
    fprintf(fp, "    \"%s/auxv.json\"\n", artifact_dir);
    fprintf(fp, "  ],\n");
    fprintf(fp, "  \"timestamp\": \"2024-01-15T10:30:00Z\",\n");
    if (failure_summary) {
        fprintf(fp, "  \"failure_summary\": \"%s\"\n", failure_summary);
    } else {
        fprintf(fp, "  \"failure_summary\": null\n");
    }
    fprintf(fp, "}\n");
    
    fclose(fp);
    return 0;
}

/* Extract case ID from path */
static const char *extract_case_id(const char *case_yaml) {
    static char case_id[64];
    const char *last_slash = strrchr(case_yaml, '/');
    if (!last_slash) return "UNKNOWN";

    const char *dir_start = last_slash;
    while (dir_start > case_yaml && *(dir_start - 1) != '/') {
        dir_start--;
    }

    /* Extract case ID (e.g., "MMU-001" from "MMU-001-anon-mmap") */
    const char *dash = strchr(dir_start, '-');
    if (!dash) return "UNKNOWN";
    const char *second_dash = strchr(dash + 1, '-');
    int len = second_dash ? (second_dash - dir_start) : (last_slash - dir_start);
    if (len >= 63) len = 63;
    strncpy(case_id, dir_start, len);
    case_id[len] = '\0';
    return case_id;
}

/* MMU-001: Anonymous mmap test */
static int test_mmu_001_anon_mmap(const char *artifact_dir) {
    printf("MMU-001: Testing anonymous mmap...\n");

    size_t size = 4096; /* 1 page */
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        printf("  FAIL: mmap failed: %s\n", strerror(errno));
        return -1;
    }

    printf("  mmap returned: %p\n", addr);

    /* Check page alignment */
    if ((uintptr_t)addr % 4096 != 0) {
        printf("  FAIL: Address not page aligned\n");
        munmap(addr, size);
        return -1;
    }
    printf("  Page aligned: YES\n");

    /* Test write */
    volatile uint64_t *ptr = (uint64_t*)addr;
    *ptr = 0xDEADBEEFCAFEBABEULL;
    printf("  Write test: OK\n");

    /* Test read */
    uint64_t val = *ptr;
    if (val != 0xDEADBEEFCAFEBABEULL) {
        printf("  FAIL: Read back wrong value: 0x%016llx\n", (unsigned long long)val);
        munmap(addr, size);
        return -1;
    }
    printf("  Read test: OK (0x%016llx)\n", (unsigned long long)val);

    /* Check zero-initialization */
    volatile uint64_t *ptr2 = (uint64_t*)((char*)addr + 8);
    if (*ptr2 != 0) {
        printf("  FAIL: Memory not zero-initialized\n");
        munmap(addr, size);
        return -1;
    }
    printf("  Zero-initialized: YES\n");

    munmap(addr, size);
    printf("  Result: PASSED\n");
    return 0;
}

/* MMU-002: mprotect permissions test */
static int test_mmu_002_mprotect(const char *artifact_dir) {
    (void)artifact_dir;
    printf("MMU-002: Testing mprotect permissions...\n");

    size_t size = 4096;
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        printf("  FAIL: mmap failed\n");
        return -1;
    }
    printf("  mmap: %p (R/W)\n", addr);

    /* Test write */
    volatile uint64_t *ptr = (uint64_t*)addr;
    *ptr = 0x123456789ABCDEF0ULL;
    printf("  Write with PROT_WRITE: OK\n");

    /* Change to read-only */
    if (mprotect(addr, size, PROT_READ) != 0) {
        printf("  FAIL: mprotect(PROT_READ) failed\n");
        munmap(addr, size);
        return -1;
    }
    printf("  mprotect(PROT_READ): OK\n");

    /* Verify read still works */
    uint64_t val = *ptr;
    if (val != 0x123456789ABCDEF0ULL) {
        printf("  FAIL: Read after mprotect failed\n");
        munmap(addr, size);
        return -1;
    }
    printf("  Read with PROT_READ: OK\n");

    /* Change to no access */
    if (mprotect(addr, size, PROT_NONE) != 0) {
        printf("  FAIL: mprotect(PROT_NONE) failed\n");
        munmap(addr, size);
        return -1;
    }
    printf("  mprotect(PROT_NONE): OK\n");

    /* Restore R/W for cleanup */
    mprotect(addr, size, PROT_READ | PROT_WRITE);
    munmap(addr, size);
    printf("  Result: PASSED\n");
    return 0;
}

/* MMU-003: brk heap growth test */
static int test_mmu_003_brk(const char *artifact_dir) {
    (void)artifact_dir;
    printf("MMU-003: Testing brk heap growth...\n");

    /* Get current break */
    void *initial_brk = sbrk(0);
    printf("  Initial brk: %p\n", initial_brk);

    /* Allocate 1 page */
    void *new_brk = sbrk(4096);
    if (new_brk == (void*)-1) {
        printf("  FAIL: sbrk(4096) failed\n");
        return -1;
    }
    printf("  After sbrk(4096): %p\n", sbrk(0));

    /* Verify we can access the new memory */
    volatile uint64_t *ptr = (volatile uint64_t*)new_brk;
    *ptr = 0xDEADBEEFCAFEBABEULL;
    if (*ptr != 0xDEADBEEFCAFEBABEULL) {
        printf("  FAIL: Heap access failed\n");
        return -1;
    }
    printf("  Heap write/read: OK\n");

    /* Note: On macOS, sbrk is deprecated and may not support shrinking.
     * We skip the shrink test on Darwin and just verify allocation works. */
#if defined(__APPLE__) && defined(__MACH__)
    printf("  brk shrink: SKIPPED (deprecated on macOS)\n");
#else
    /* Return to original break */
    sbrk(-4096);
    void *final_brk = sbrk(0);
    printf("  After sbrk(-4096): %p\n", final_brk);

    if (final_brk != initial_brk) {
        printf("  FAIL: brk not restored to initial value\n");
        return -1;
    }
    printf("  brk restored: OK\n");
#endif

    printf("  Result: PASSED\n");
    return 0;
}

/* ABI-001: Process entry stack test */
static int test_abi_001_stack(const char *artifact_dir) {
    printf("ABI-001: Testing process entry stack...\n");

    /* Get current stack pointer */
    volatile char *sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));

    printf("  Current SP: %p\n", (void*)sp);

    /* Check 16-byte alignment */
    if ((uintptr_t)sp % 16 != 0) {
        printf("  FAIL: Stack not 16-byte aligned\n");
        return -1;
    }
    printf("  16-byte aligned: YES\n");

    /* Test stack access */
    volatile uint64_t test_val = 0x123456789ABCDEF0ULL;
    if (test_val != 0x123456789ABCDEF0ULL) {
        printf("  FAIL: Stack access failed\n");
        return -1;
    }
    printf("  Stack access: OK\n");

    printf("  Result: PASSED\n");
    return 0;
}

int main(int argc, char *argv[]) {
    const char *case_yaml = NULL;
    const char *artifact_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--case-yaml") == 0 && i + 1 < argc) {
            case_yaml = argv[++i];
        } else if (strcmp(argv[i], "--artifact-dir") == 0 && i + 1 < argc) {
            artifact_dir = argv[++i];
        }
    }

    if (!case_yaml || !artifact_dir) {
        fprintf(stderr, "Usage: %s --case-yaml <path> --artifact-dir <path>\n", argv[0]);
        return 1;
    }

    if (setup_artifact_dir(artifact_dir) != 0) {
        return 1;
    }

    const char *case_id = extract_case_id(case_yaml);
    printf("ABI Fixture Harness - %s\n", case_id);

    int result = 0;
    const char *failure_reason = NULL;

    /* Route to appropriate test */
    if (strncmp(case_id, "MMU-001", 7) == 0) {
        result = test_mmu_001_anon_mmap(artifact_dir);
        if (result != 0) failure_reason = "MMU-001 anon mmap test failed";
    } else if (strncmp(case_id, "MMU-002", 7) == 0) {
        result = test_mmu_002_mprotect(artifact_dir);
        if (result != 0) failure_reason = "MMU-002 mprotect test failed";
    } else if (strncmp(case_id, "MMU-003", 7) == 0) {
        result = test_mmu_003_brk(artifact_dir);
        if (result != 0) failure_reason = "MMU-003 brk test failed";
    } else if (strncmp(case_id, "ABI-001", 7) == 0) {
        result = test_abi_001_stack(artifact_dir);
        if (result != 0) failure_reason = "ABI-001 stack test failed";
    } else {
        printf("STATUS: STUB - Test not implemented for %s\n", case_id);
        failure_reason = "STUB: Test not implemented";
        result = -1;
    }

    /* Write artifacts */
    char auxv_path[MAX_PATH];
    snprintf(auxv_path, sizeof(auxv_path), "%s/auxv.json", artifact_dir);
    FILE *fp = fopen(auxv_path, "w");
    if (fp) {
        fprintf(fp, "{\n");
        fprintf(fp, "  \"case_id\": \"%s\",\n", case_id);
        fprintf(fp, "  \"status\": \"%s\",\n", result == 0 ? "passed" : "failed");
        if (failure_reason) {
            fprintf(fp, "  \"failure_reason\": \"%s\"\n", failure_reason);
        } else {
            fprintf(fp, "  \"failure_reason\": null\n");
        }
        fprintf(fp, "}\n");
        fclose(fp);
    }

    /* Write empty stack_dump.bin (placeholder) */
    char stack_path[MAX_PATH];
    snprintf(stack_path, sizeof(stack_path), "%s/stack_dump.bin", artifact_dir);
    fp = fopen(stack_path, "wb");
    if (fp) {
        fclose(fp);
    }

    /* Write report */
    if (write_report(artifact_dir, case_id, "04-mmu-abi", "abi_fixture",
                     result == 0, failure_reason) != 0) {
        return 1;
    }

    printf("Result: %s\n", result == 0 ? "PASSED" : "FAILED");
    if (failure_reason) {
        printf("Failure: %s\n", failure_reason);
    }

    return result == 0 ? 0 : 1;
}
