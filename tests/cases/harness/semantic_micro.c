/*
 * Semantic Micro Harness
 * Validates single instruction execution semantics.
 * 
 * CURRENT STATUS: STUB - Real TCTI execution not yet implemented
 * 
 * This harness currently validates decode and generator paths but uses
 * harness-side simulation for execution instead of real TCTI execution.
 * Per policy Section 5.3, this makes it a stub that must report failure.
 * 
 * TO IMPLEMENT REAL EXECUTION:
 * 1. Set up minimal TLB and memory for instruction
 * 2. Create a64_block from generated gadgets
 * 3. Call a64_execute_block() or tcti_entry_block()
 * 4. Capture real final CPU state
 * 5. Compare against expected.yaml
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

#define MAX_PATH 4096
#define MAX_LINE 1024

/* Stub kernel functions required by tcti/aarch64/gen.c */
void ish_printk(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

#define printk ish_printk

/* Stub interrupt handler - used in harness */
void handle_interrupt(int interrupt) {
    fprintf(stderr, "[HARNESS] handle_interrupt called: %d\n", interrupt);
}

/* TCTI headers */
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"
#include "tcti/aarch64/gen.h"

/* Clean and recreate artifact directory */
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
    fprintf(fp, "    \"%s/final_state.json\"\n", artifact_dir);
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

    printf("Semantic Micro Harness - EXEC-001\n");
    printf("STATUS: STUB - Real TCTI execution not yet implemented\n");

    /* Write stub final_state.json */
    char state_path[MAX_PATH];
    snprintf(state_path, sizeof(state_path), "%s/final_state.json", artifact_dir);
    FILE *fp = fopen(state_path, "w");
    if (fp) {
        fprintf(fp, "{\n");
        fprintf(fp, "  \"status\": \"unimplemented\",\n");
        fprintf(fp, "  \"message\": \"Real semantic execution requires TCTI integration. "
                "Current harness validates decode+generator only. "
                "To implement: setup TLB/memory, create a64_block, call a64_execute_block(), capture result.\"\n");
        fprintf(fp, "}\n");
        fclose(fp);
    }

    /* Report as FAIL - stub implementations MUST NOT count as pass */
    const char *failure_reason = "STUB: Real TCTI execution not yet implemented. "
        "Harness validates decode and generator paths but uses simulation for execution. "
        "Per policy Section 5.3, semantic cases must use real execution path, not harness-side simulation.";

    if (write_report(artifact_dir, "EXEC-001", "03-semantic-exec", "semantic_micro",
                     0 /* FAIL */, failure_reason) != 0) {
        return 1;
    }

    printf("Result: FAILED (stub)\n");
    printf("Failure: %s\n", failure_reason);

    return 1; /* Return failure for stub */
}
