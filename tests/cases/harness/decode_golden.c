/*
 * Decode Golden Harness
 * Validates decoder output against expected values from authority chain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "emu/aarch64/decode.h"

#define MAX_PATH 4096
#define MAX_LINE 1024

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

typedef struct {
    char name[64];
    char encoding_hex_le[16];
    int expected_rd;
    int expected_rn;
    int expected_imm12;
    int expected_set_flags;
} test_vector_t;

static int parse_hex_encoding(const char *hex_str, uint32_t *encoding) {
    /* Parse little-endian hex string to get instruction word */
    unsigned int bytes[4];
    if (sscanf(hex_str, "%2x%2x%2x%2x", &bytes[0], &bytes[1], &bytes[2], &bytes[3]) != 4) {
        return -1;
    }
    /* Convert LE bytes to instruction word */
    *encoding = (bytes[3] << 24) | (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
    return 0;
}

/* Write decoded.json with actual decoder output */
static int write_decoded_json(const char *artifact_dir, test_vector_t *vectors, 
                               int num_vectors, a64_instr_t *decoded, int *passed_flags) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/decoded.json", artifact_dir);
    
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot write decoded.json: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"vectors\": [\n");
    
    int all_passed = 1;
    int passed_count = 0;
    
    for (int i = 0; i < num_vectors; i++) {
        if (i > 0) fprintf(fp, ",\n");
        
        a64_instr_t *instr = &decoded[i];
        int vector_passed = passed_flags[i];
        if (vector_passed) passed_count++;
        if (!vector_passed) all_passed = 0;
        
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"name\": \"%s\",\n", vectors[i].name);
        fprintf(fp, "      \"input\": {\n");
        fprintf(fp, "        \"encoding_hex_le\": \"%s\",\n", vectors[i].encoding_hex_le);
        fprintf(fp, "        \"raw_value\": \"0x%08x\"\n", instr->raw);
        fprintf(fp, "      },\n");
        fprintf(fp, "      \"decoded\": {\n");
        fprintf(fp, "        \"category\": %d,\n", instr->cat);
        fprintf(fp, "        \"rd\": %d,\n", instr->Rd);
        fprintf(fp, "        \"rn\": %d,\n", instr->Rn);
        fprintf(fp, "        \"imm\": %lld,\n", (long long)instr->imm);
        fprintf(fp, "        \"set_flags\": %s\n", instr->set_flags ? "true" : "false");
        fprintf(fp, "      },\n");
        fprintf(fp, "      \"expected\": {\n");
        fprintf(fp, "        \"rd\": %d,\n", vectors[i].expected_rd);
        fprintf(fp, "        \"rn\": %d,\n", vectors[i].expected_rn);
        fprintf(fp, "        \"imm12\": %d,\n", vectors[i].expected_imm12);
        fprintf(fp, "        \"set_flags\": %s\n", vectors[i].expected_set_flags ? "true" : "false");
        fprintf(fp, "      },\n");
        fprintf(fp, "      \"passed\": %s\n", vector_passed ? "true" : "false");
        fprintf(fp, "    }");
    }
    
    fprintf(fp, "\n  ],\n");
    fprintf(fp, "  \"summary\": {\n");
    fprintf(fp, "    \"total\": %d,\n", num_vectors);
    fprintf(fp, "    \"passed\": %d,\n", passed_count);
    fprintf(fp, "    \"failed\": %d,\n", num_vectors - passed_count);
    fprintf(fp, "    \"all_passed\": %s\n", all_passed ? "true" : "false");
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"status\": \"decoded\"\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    return all_passed ? 0 : 1;
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
    fprintf(fp, "    \"%s/report.json\",\n", artifact_dir);
    fprintf(fp, "    \"%s/decoded.json\"\n", artifact_dir);
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

/* Test vectors from expected.yaml - verified with llvm-mc */
static test_vector_t test_vectors[] = {
    {
        .name = "add_x0_x1_imm1",
        .encoding_hex_le = "20040091",  /* llvm-mc verified: [0x20,0x04,0x00,0x91] */
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_imm12 = 1,
        .expected_set_flags = 0
    },
    {
        .name = "sub_x2_x3_imm8",
        .encoding_hex_le = "622000d1",  /* llvm-mc verified: [0x62,0x20,0x00,0xd1] */
        .expected_rd = 2,
        .expected_rn = 3,
        .expected_imm12 = 8,
        .expected_set_flags = 0
    },
    {
        .name = "cmp_x4_imm0",
        .encoding_hex_le = "9f0000f1",  /* llvm-mc verified: [0x9f,0x00,0x00,0xf1] */
        .expected_rd = 31,               /* xzr = register 31 */
        .expected_rn = 4,
        .expected_imm12 = 0,
        .expected_set_flags = 1          /* CMP uses SUBS which sets flags */
    }
};

#define NUM_VECTORS (sizeof(test_vectors) / sizeof(test_vectors[0]))

int main(int argc, char *argv[]) {
    const char *case_yaml = NULL;
    const char *artifact_dir = NULL;
    int passed = 1;
    const char *failure = NULL;
    
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
    
    printf("Decode Golden Harness\n");
    printf("Testing %zu vectors\n", NUM_VECTORS);
    
    a64_instr_t decoded[NUM_VECTORS];
    int passed_flags[NUM_VECTORS];
    int fail_count = 0;
    
    /* Decode each vector */
    for (size_t i = 0; i < NUM_VECTORS; i++) {
        test_vector_t *vec = &test_vectors[i];
        uint32_t encoding;
        
        if (parse_hex_encoding(vec->encoding_hex_le, &encoding) != 0) {
            fprintf(stderr, "Error: Failed to parse encoding '%s' for %s\n", 
                    vec->encoding_hex_le, vec->name);
            passed_flags[i] = 0;
            fail_count++;
            continue;
        }
        
        printf("  %s: encoding = 0x%08x\n", vec->name, encoding);
        
        /* Call the real decoder */
        int ret = a64_decode(encoding, &decoded[i]);
        
        if (ret != 0) {
            fprintf(stderr, "Error: Decoder returned %d for %s\n", ret, vec->name);
            passed_flags[i] = 0;
            fail_count++;
            continue;
        }
        
        /* Check expected values */
        int vector_passed = 1;
        
        if (decoded[i].Rd != vec->expected_rd) {
            printf("    FAIL: Rd mismatch (got %d, expected %d)\n", 
                   decoded[i].Rd, vec->expected_rd);
            vector_passed = 0;
        }
        
        if (decoded[i].Rn != vec->expected_rn) {
            printf("    FAIL: Rn mismatch (got %d, expected %d)\n", 
                   decoded[i].Rn, vec->expected_rn);
            vector_passed = 0;
        }
        
        if (decoded[i].imm != vec->expected_imm12) {
            printf("    FAIL: imm mismatch (got %lld, expected %d)\n", 
                   (long long)decoded[i].imm, vec->expected_imm12);
            vector_passed = 0;
        }
        
        if (decoded[i].set_flags != vec->expected_set_flags) {
            printf("    FAIL: set_flags mismatch (got %d, expected %d)\n", 
                   decoded[i].set_flags, vec->expected_set_flags);
            vector_passed = 0;
        }
        
        passed_flags[i] = vector_passed;
        if (!vector_passed) {
            fail_count++;
        } else {
            printf("    PASS\n");
        }
    }
    
    /* Write decoded.json */
    if (write_decoded_json(artifact_dir, test_vectors, NUM_VECTORS, 
                           decoded, passed_flags) != 0) {
        passed = 0;
        failure = "Some vectors failed decoding validation";
    }
    
    /* Check success threshold (all must pass) */
    if (fail_count > 0) {
        passed = 0;
        failure = "Not all vectors passed";
    }
    
    printf("\nResults: %zu/%zu passed\n", NUM_VECTORS - fail_count, NUM_VECTORS);
    
    /* Write report */
    if (write_report(artifact_dir, "DEC-001", "01-decode", "decode_golden", 
                     passed, failure) != 0) {
        return 1;
    }
    
    return passed ? 0 : 1;
}
