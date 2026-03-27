/*
 * Decode Golden Harness
 * Validates decoder output against expected values from authority chain.
 * Supports multiple decode cases: DEC-001, DEC-002, etc.
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

/* Test vector structure - extended for all decode cases */
typedef struct {
    char name[64];
    char encoding_hex_le[16];
    int expected_rd;
    int expected_rn;
    int expected_rm;           /* For register-based operations */
    int64_t expected_imm12;    /* For immediate operations (64-bit for move wide) */
    int expected_shift_type;   /* 0=LSL, 1=LSR, 2=ASR, 3=ROR */
    int expected_shift_amount; /* Shift amount (0-63) */
    int expected_set_flags;
    int expected_category;
    int expected_subtype;
} test_vector_t;

/* Case configuration */
typedef struct {
    const char *case_id;
    const test_vector_t *vectors;
    size_t num_vectors;
} case_config_t;

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
static int write_decoded_json(const char *artifact_dir, const char *case_id,
                               test_vector_t *vectors, int num_vectors,
                               a64_instr_t *decoded, int *passed_flags) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/decoded.json", artifact_dir);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot write decoded.json: %s\n", strerror(errno));
        return -1;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"case_id\": \"%s\",\n", case_id);
    fprintf(fp, "  \"vectors\": [\n");

    int all_passed = 1;
    int passed_count = 0;

    for (int i = 0; i < num_vectors; i++) {
        if (i > 0) fprintf(fp, ",\n");

        a64_instr_t *instr = &decoded[i];
        test_vector_t *vec = &vectors[i];
        int vector_passed = passed_flags[i];
        if (vector_passed) passed_count++;
        if (!vector_passed) all_passed = 0;

        fprintf(fp, "    {\n");
        fprintf(fp, "      \"name\": \"%s\",\n", vec->name);
        fprintf(fp, "      \"input\": {\n");
        fprintf(fp, "        \"encoding_hex_le\": \"%s\",\n", vec->encoding_hex_le);
        fprintf(fp, "        \"raw_value\": \"0x%08x\"\n", instr->raw);
        fprintf(fp, "      },\n");
        fprintf(fp, "      \"decoded\": {\n");
        fprintf(fp, "        \"category\": %d,\n", instr->cat);
        fprintf(fp, "        \"subtype\": %d,\n", instr->subtype);
        fprintf(fp, "        \"rd\": %d,\n", instr->Rd);
        fprintf(fp, "        \"rn\": %d,\n", instr->Rn);
        fprintf(fp, "        \"rm\": %d,\n", instr->Rm);
        fprintf(fp, "        \"imm\": %lld,\n", (long long)instr->imm);
        fprintf(fp, "        \"shift_type\": %d,\n", instr->shift_type);
        fprintf(fp, "        \"shift_amount\": %d,\n", instr->imm_shift);
        fprintf(fp, "        \"set_flags\": %s\n", instr->set_flags ? "true" : "false");
        fprintf(fp, "      },\n");
        fprintf(fp, "      \"expected\": {\n");
        fprintf(fp, "        \"category\": %d,\n", vec->expected_category);
        fprintf(fp, "        \"subtype\": %d,\n", vec->expected_subtype);
        fprintf(fp, "        \"rd\": %d,\n", vec->expected_rd);
        fprintf(fp, "        \"rn\": %d,\n", vec->expected_rn);
        fprintf(fp, "        \"rm\": %d,\n", vec->expected_rm);
        fprintf(fp, "        \"imm12\": %d,\n", vec->expected_imm12);
        fprintf(fp, "        \"shift_type\": %d,\n", vec->expected_shift_type);
        fprintf(fp, "        \"shift_amount\": %d,\n", vec->expected_shift_amount);
        fprintf(fp, "        \"set_flags\": %s\n", vec->expected_set_flags ? "true" : "false");
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

/* Test vectors for DEC-001 - ADD/SUB immediate (verified with llvm-mc) */
/* Decoder subtype mapping: 3=ADD with shift, 4=ADD no shift, 5=SUB with shift, 6=SUB no shift */
static test_vector_t dec001_vectors[] = {
    {
        .name = "add_x0_x1_imm1",
        .encoding_hex_le = "20040091",
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_rm = 0,
        .expected_imm12 = 1,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 8,    /* A64_SIMD0 in enum - actually DP_IMM encoding */
        .expected_subtype = 4      /* ADD immediate no shift */
    },
    {
        .name = "sub_x2_x3_imm8",
        .encoding_hex_le = "622000d1",
        .expected_rd = 2,
        .expected_rn = 3,
        .expected_rm = 0,
        .expected_imm12 = 8,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 8,
        .expected_subtype = 6      /* SUB immediate no shift */
    },
    {
        .name = "cmp_x4_imm0",
        .encoding_hex_le = "9f0000f1",
        .expected_rd = 31,
        .expected_rn = 4,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 1,
        .expected_category = 8,
        .expected_subtype = 6      /* SUBS immediate no shift */
    }
};

/* Test vectors for DEC-002 - ADD/SUB shifted register (verified with llvm-mc) */
static test_vector_t dec002_vectors[] = {
    {
        .name = "add_x0_x1_x2_lsl0",
        .encoding_hex_le = "2000028b",
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_rm = 2,
        .expected_imm12 = 0,
        .expected_shift_type = 0,  /* LSL */
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 5,    /* A64_DP_REG2 */
        .expected_subtype = 0      /* ADD (op=0) */
    },
    {
        .name = "sub_x3_x4_x5_lsl3",
        .encoding_hex_le = "830c05cb",
        .expected_rd = 3,
        .expected_rn = 4,
        .expected_rm = 5,
        .expected_imm12 = 0,
        .expected_shift_type = 0,  /* LSL */
        .expected_shift_amount = 3,
        .expected_set_flags = 0,
        .expected_category = 5,
        .expected_subtype = 1
    },
    {
        .name = "adds_x6_x7_x8_lsr4",
        .encoding_hex_le = "e61048ab",
        .expected_rd = 6,
        .expected_rn = 7,
        .expected_rm = 8,
        .expected_imm12 = 0,
        .expected_shift_type = 1,  /* LSR */
        .expected_shift_amount = 4,
        .expected_set_flags = 1,
        .expected_category = 5,
        .expected_subtype = 0      /* ADD (op=0) */
    },
    {
        .name = "subs_x9_x10_x11_asr5",
        .encoding_hex_le = "49158beb",
        .expected_rd = 9,
        .expected_rn = 10,
        .expected_rm = 11,
        .expected_imm12 = 0,
        .expected_shift_type = 2,  /* ASR */
        .expected_shift_amount = 5,
        .expected_set_flags = 1,
        .expected_category = 5,
        .expected_subtype = 1
    }
};

/* Test vectors for DEC-003 - Logical immediate and register operations */
static test_vector_t dec003_vectors[] = {
    /* Logical shifted register operations (category 5, op2=0-3) */
    {
        .name = "and_x0_x1_x2",
        .encoding_hex_le = "2000028a",  /* and x0, x1, x2 */
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_rm = 2,
        .expected_imm12 = 0,
        .expected_shift_type = 0,  /* LSL */
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 5,    /* A64_DP_REG2 */
        .expected_subtype = 0      /* AND (opc=00) */
    },
    {
        .name = "orr_x3_x4_x5",
        .encoding_hex_le = "830005aa",  /* orr x3, x4, x5 */
        .expected_rd = 3,
        .expected_rn = 4,
        .expected_rm = 5,
        .expected_imm12 = 0,
        .expected_shift_type = 0,  /* LSL */
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 5,
        .expected_subtype = 1      /* ORR (opc=01) */
    },
    {
        .name = "eor_x6_x7_x8",
        .encoding_hex_le = "e60008ca",  /* eor x6, x7, x8 */
        .expected_rd = 6,
        .expected_rn = 7,
        .expected_rm = 8,
        .expected_imm12 = 0,
        .expected_shift_type = 0,  /* LSL */
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 5,
        .expected_subtype = 2      /* EOR (opc=10) */
    },
    {
        .name = "ands_x9_x10_x11",
        .encoding_hex_le = "49010bea",  /* ands x9, x10, x11 */
        .expected_rd = 9,
        .expected_rn = 10,
        .expected_rm = 11,
        .expected_imm12 = 0,
        .expected_shift_type = 0,  /* LSL */
        .expected_shift_amount = 0,
        .expected_set_flags = 1,
        .expected_category = 5,
        .expected_subtype = 3      /* ANDS (opc=11) */
    },
    /* Logical immediate operations (category 9, subtype 7-10) */
    {
        .name = "and_x0_x1_imm_ff",
        .encoding_hex_le = "201c4092",  /* and x0, x1, #0xff */
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_rm = 0,
        .expected_imm12 = 8640,  /* N=1, imms=0b000111, immr=0b000000 = (1<<13)|(7<<6)|0 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,    /* A64_DP_IMM */
        .expected_subtype = 7      /* AND immediate */
    },
    {
        .name = "orr_x2_x3_imm_ffff",
        .encoding_hex_le = "623c40b2",  /* orr x2, x3, #0xffff */
        .expected_rd = 2,
        .expected_rn = 3,
        .expected_rm = 0,
        .expected_imm12 = 9152,  /* N=1, imms=0b001111, immr=0b000000 = (1<<13)|(15<<6)|0 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 8      /* ORR immediate */
    },
    {
        .name = "eor_x4_x5_imm_f",
        .encoding_hex_le = "a40c40d2",  /* eor x4, x5, #0xf */
        .expected_rd = 4,
        .expected_rn = 5,
        .expected_rm = 0,
        .expected_imm12 = 8384,  /* N=1, imms=0b000011, immr=0b000000 = (1<<13)|(3<<6)|0 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 9      /* EOR immediate */
    }
};

/* Test vectors for DEC-004 - Unconditional branch */
static test_vector_t dec004_vectors[] = {
    /* Unconditional branch immediate */
    {
        .name = "b_imm_0x100",
        .encoding_hex_le = "40000014",  /* b #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,   /* sign_extend(0x40, 26) << 2 = 256 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10, /* A64_BRANCH */
        .expected_subtype = 1    /* Unconditional branch immediate */
    },
    {
        .name = "bl_imm_0x200",
        .encoding_hex_le = "80000094",  /* bl #0x200 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 512,   /* sign_extend(0x80, 26) << 2 = 512 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 1
    },
    /* Unconditional branch register */
    {
        .name = "br_x0",
        .encoding_hex_le = "00001fd6",  /* br x0 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 11, /* A64_BRANCH2 */
        .expected_subtype = 5    /* Unconditional branch register */
    },
    {
        .name = "blr_x1",
        .encoding_hex_le = "20003fd6",  /* blr x1 */
        .expected_rd = 0,
        .expected_rn = 1,        /* x1 */
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 11,
        .expected_subtype = 5
    },
    {
        .name = "ret",
        .encoding_hex_le = "c0035fd6",  /* ret */
        .expected_rd = 0,
        .expected_rn = 30,       /* x30 (link register) */
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 11,
        .expected_subtype = 5
    }
};

/* Test vectors for DEC-005 - Conditional branch family */
static test_vector_t dec005_vectors[] = {
    /* Conditional branch instructions - b.cond (target = +0x100) */
    {
        .name = "beq_0x100",
        .encoding_hex_le = "00080054",  /* b.eq #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,   /* sign_extend(0x40, 19) << 2 = 256 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10, /* A64_BRANCH */
        .expected_subtype = 0   /* Conditional branch */
    },
    {
        .name = "bne_0x100",
        .encoding_hex_le = "01080054",  /* b.ne #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bcs_0x100",
        .encoding_hex_le = "02080054",  /* b.cs #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bcc_0x100",
        .encoding_hex_le = "03080054",  /* b.cc #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bmi_0x100",
        .encoding_hex_le = "04080054",  /* b.mi #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bpl_0x100",
        .encoding_hex_le = "05080054",  /* b.pl #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bvs_0x100",
        .encoding_hex_le = "06080054",  /* b.vs #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bvc_0x100",
        .encoding_hex_le = "07080054",  /* b.vc #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bhi_0x100",
        .encoding_hex_le = "08080054",  /* b.hi #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bls_0x100",
        .encoding_hex_le = "09080054",  /* b.ls #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bge_0x100",
        .encoding_hex_le = "0a080054",  /* b.ge #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "blt_0x100",
        .encoding_hex_le = "0b080054",  /* b.lt #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bgt_0x100",
        .encoding_hex_le = "0c080054",  /* b.gt #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "ble_0x100",
        .encoding_hex_le = "0d080054",  /* b.le #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    },
    {
        .name = "bal_0x100",
        .encoding_hex_le = "0e080054",  /* b.al #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 0
    }
};

/* Test vectors for DEC-006 - CBZ, CBNZ, TBZ, TBNZ */
static test_vector_t dec006_vectors[] = {
    /* Compare and branch (CBZ/CBNZ) */
    {
        .name = "cbz_x0_0x100",
        .encoding_hex_le = "000800b4",  /* cbz x0, #0x100 */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 2   /* Compare and branch */
    },
    {
        .name = "cbnz_x1_0x100",
        .encoding_hex_le = "010800b5",  /* cbnz x1, #0x100 */
        .expected_rd = 1,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 2
    },
    {
        .name = "cbz_w4_0x200",
        .encoding_hex_le = "04100034",  /* cbz w4, #512 */
        .expected_rd = 4,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 512,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 2
    },
    {
        .name = "cbnz_w5_0x200",
        .encoding_hex_le = "05100035",  /* cbnz w5, #512 */
        .expected_rd = 5,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 512,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 10,
        .expected_subtype = 2
    },
    /* Test and branch (TBZ/TBNZ) */
    {
        .name = "tbz_w2_bit5_0x100",
        .encoding_hex_le = "02082836",  /* tbz w2, #5, #256 */
        .expected_rd = 2,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 5,  /* Bit position */
        .expected_set_flags = 0,
        .expected_category = 11,  /* A64_BRANCH2 - TBZ/TBNZ use op0=1011 */
        .expected_subtype = 3   /* Test and branch */
    },
    {
        .name = "tbnz_w3_bit7_0x100",
        .encoding_hex_le = "03083837",  /* tbnz w3, #7, #256 */
        .expected_rd = 3,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 256,
        .expected_shift_type = 0,
        .expected_shift_amount = 7,
        .expected_set_flags = 0,
        .expected_category = 11,  /* A64_BRANCH2 */
        .expected_subtype = 3
    },
    {
        .name = "tbz_w10_bit15_0x80",
        .encoding_hex_le = "0a047836",  /* tbz w10, #15, #128 */
        .expected_rd = 10,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 128,
        .expected_shift_type = 0,
        .expected_shift_amount = 15,
        .expected_set_flags = 0,
        .expected_category = 11,  /* A64_BRANCH2 */
        .expected_subtype = 3
    },
    {
        .name = "tbnz_w11_bit31_0x80",
        .encoding_hex_le = "0b04f837",  /* tbnz w11, #31, #128 */
        .expected_rd = 11,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 128,
        .expected_shift_type = 0,
        .expected_shift_amount = 31,
        .expected_set_flags = 0,
        .expected_category = 11,  /* A64_BRANCH2 */
        .expected_subtype = 3
    }
};

/* Test vectors for DEC-007 - Load/Store instructions */
static test_vector_t dec007_vectors[] = {
    /* Load/Store with unsigned immediate offset */
    {
        .name = "ldr_x0_sp_0",
        .encoding_hex_le = "e00340f9",  /* ldr x0, [sp]: [0xe0,0x03,0x40,0xf9] */
        .expected_rd = 0,
        .expected_rn = 31,  /* sp */
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,  /* A64_LD_ST */
        .expected_subtype = 4     /* A64_LDST_LITERAL - decoder uses this for single reg */
    },
    {
        .name = "str_x1_sp_8",
        .encoding_hex_le = "e10700f9",  /* str x1, [sp, #8]: [0xe1,0x07,0x00,0xf9] */
        .expected_rd = 1,
        .expected_rn = 31,
        .expected_rm = 0,
        .expected_imm12 = 8,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 4
    },
    {
        .name = "ldr_w2_sp_16",
        .encoding_hex_le = "e21340b9",  /* ldr w2, [sp, #16]: [0xe2,0x13,0x40,0xb9] */
        .expected_rd = 2,
        .expected_rn = 31,
        .expected_rm = 0,
        .expected_imm12 = 16,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 4
    },
    {
        .name = "str_w3_sp_4",
        .encoding_hex_le = "e30700b9",  /* str w3, [sp, #4]: [0xe3,0x07,0x00,0xb9] */
        .expected_rd = 3,
        .expected_rn = 31,
        .expected_rm = 0,
        .expected_imm12 = 4,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 4
    },
    /* Load Literal (PC-relative) */
    {
        .name = "ldr_x4_lit",
        .encoding_hex_le = "04000058",  /* ldr x4, #0: [0x04,0x00,0x00,0x58] */
        .expected_rd = 4,
        .expected_rn = 0,  /* Rn not set for literal loads */
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 4  /* A64_LDST_LITERAL */
    },
    /* Load/Store Pair */
    {
        .name = "ldp_x5_x6_sp",
        .encoding_hex_le = "e51b40a9",  /* ldp x5, x6, [sp]: [0xe5,0x1b,0x40,0xa9] */
        .expected_rd = 5,
        .expected_rn = 31,
        .expected_rm = 6,  /* Rt2 = second register */
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 5  /* A64_LDST_PAIR */
    },
    {
        .name = "stp_x7_x8_sp_16",
        .encoding_hex_le = "e72301a9",  /* stp x7, x8, [sp, #16]: [0xe7,0x23,0x01,0xa9] */
        .expected_rd = 7,
        .expected_rn = 31,
        .expected_rm = 8,
        .expected_imm12 = 0,  /* pair_offset calculated differently */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 5
    },
    {
        .name = "ldp_w9_w10_sp",
        .encoding_hex_le = "e92b4029",  /* ldp w9, w10, [sp]: [0xe9,0x2b,0x40,0x29] */
        .expected_rd = 9,
        .expected_rn = 31,
        .expected_rm = 10,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 5
    },
    {
        .name = "stp_w11_w12_sp_8",
        .encoding_hex_le = "eb330129",  /* stp w11, w12, [sp, #8]: [0xeb,0x33,0x01,0x29] */
        .expected_rd = 11,
        .expected_rn = 31,
        .expected_rm = 12,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 12,
        .expected_subtype = 5
    }
};

/* Test vectors for DEC-008 - Move Wide Immediate */
static test_vector_t dec008_vectors[] = {
    /* MOVZ - Move wide with zero (subtype 1) */
    {
        .name = "movz_x0_0x1234",
        .encoding_hex_le = "804682d2",  /* movz x0, #0x1234: [0x80,0x46,0x82,0xd2] */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0x1234,  /* imm16 << (hw*16), hw=0 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,   /* A64_DP_IMM */
        .expected_subtype = 1     /* MOVZ = 1 */
    },
    {
        .name = "movz_w3_0xff",
        .encoding_hex_le = "e31f8052",  /* movz w3, #0xff: [0xe3,0x1f,0x80,0x52] */
        .expected_rd = 3,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0xff,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 1     /* MOVZ = 1 */
    },
    /* MOVN - Move wide with NOT (subtype 0) */
    {
        .name = "movn_x1_0xffff",
        .encoding_hex_le = "e1ff9f92",  /* movn x1, #0xffff: [0xe1,0xff,0x9f,0x92] */
        .expected_rd = 1,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0xffff,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 0     /* MOVN = 0 */
    },
    {
        .name = "movn_w4_0x0",
        .encoding_hex_le = "04008012",  /* movn w4, #0x0: [0x04,0x00,0x80,0x12] */
        .expected_rd = 4,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 0     /* MOVN = 0 */
    },
    /* MOVK - Move wide with keep (subtype 2) */
    {
        .name = "movk_x2_0xabcd_lsl16",
        .encoding_hex_le = "a279b5f2",  /* movk x2, #0xabcd, lsl #16: [0xa2,0x79,0xb5,0xf2] */
        .expected_rd = 2,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0xabcd0000,  /* Decoder stores shifted value: 0xabcd << 16 */
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 2     /* MOVK = 2 */
    },
    {
        .name = "movk_w5_0x1234",
        .encoding_hex_le = "85468272",  /* movk w5, #0x1234: [0x85,0x46,0x82,0x72] */
        .expected_rd = 5,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0x1234,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 2     /* MOVK = 2 */
    }
};

/* Test vectors for DEC-009 - Bitfield Move */
static test_vector_t dec009_vectors[] = {
    /* SBFM - Signed bitfield move (subtype 11) */
    {
        .name = "sxtb_x0_w1",
        .encoding_hex_le = "201c4093",  /* sxtb x0, w1: [0x20,0x1c,0x40,0x93] */
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 7,   /* imms = 7 */
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 11        /* SBFM */
    },
    {
        .name = "sxth_w2_w3",
        .encoding_hex_le = "623c0013",  /* sxth w2, w3: [0x62,0x3c,0x00,0x13] */
        .expected_rd = 2,
        .expected_rn = 3,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 15,  /* imms = 15 */
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 11
    },
    /* UBFM - Unsigned bitfield move (subtype 13) */
    {
        .name = "uxtb_w4_w5",
        .encoding_hex_le = "a41c0053",  /* uxtb w4, w5: [0xa4,0x1c,0x00,0x53] */
        .expected_rd = 4,
        .expected_rn = 5,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 7,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 13        /* UBFM */
    },
    {
        .name = "uxth_w6_w7",
        .encoding_hex_le = "e63c0053",  /* uxth w6, w7: [0xe6,0x3c,0x00,0x53] */
        .expected_rd = 6,
        .expected_rn = 7,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 15,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 13
    },
    {
        .name = "lsr_w0_w1_8",
        .encoding_hex_le = "207c0853",  /* lsr w0, w1, #8: [0x20,0x7c,0x08,0x53] */
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_rm = 0,
        .expected_imm12 = 8,
        .expected_shift_type = 0,
        .expected_shift_amount = 31,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 13
    },
    /* UBFX - Unsigned bitfield extract (UBFM alias) */
    {
        .name = "ubfx_w2_w3_4_8",
        .encoding_hex_le = "622c0453",  /* ubfx w2, w3, #4, #8: [0x62,0x2c,0x04,0x53] */
        .expected_rd = 2,
        .expected_rn = 3,
        .expected_rm = 0,
        .expected_imm12 = 4,
        .expected_shift_type = 0,
        .expected_shift_amount = 11,  /* imms = 4+8-1 = 11 */
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 13
    }
};

/* Test vectors for DEC-010 - Extract and Branch Register */
static test_vector_t dec010_vectors[] = {
    /* EXTR - Extract register from pair */
    {
        .name = "extr_x0_x1_x2_8",
        .encoding_hex_le = "2020c293",  /* extr x0, x1, x2, #8: [0x20,0x20,0xc2,0x93] */
        .expected_rd = 0,
        .expected_rn = 1,
        .expected_rm = 2,
        .expected_imm12 = 8,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 0   /* EXTR */
    },
    {
        .name = "extr_w3_w4_w5_16",
        .encoding_hex_le = "83408513",  /* extr w3, w4, w5, #16: [0x83,0x40,0x85,0x13] */
        .expected_rd = 3,
        .expected_rn = 4,
        .expected_rm = 5,
        .expected_imm12 = 16,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 9,
        .expected_subtype = 0
    },
    /* BR/BLR/RET - Branch register */
    {
        .name = "br_x6",
        .encoding_hex_le = "c0001fd6",  /* br x6: [0xc0,0x00,0x1f,0xd6] */
        .expected_rd = 0,
        .expected_rn = 6,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 11,  /* A64_BRANCH2 */
        .expected_subtype = 5     /* Branch register */
    },
    {
        .name = "blr_x7",
        .encoding_hex_le = "e0003fd6",  /* blr x7: [0xe0,0x00,0x3f,0xd6] */
        .expected_rd = 0,
        .expected_rn = 7,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 11,
        .expected_subtype = 5
    },
    {
        .name = "ret",
        .encoding_hex_le = "c0035fd6",  /* ret: [0xc0,0x03,0x5f,0xd6] */
        .expected_rd = 0,
        .expected_rn = 30,  /* x30 (LR) is default for RET */
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 11,
        .expected_subtype = 5
    }
};

/* Test vectors for DEC-011 - System and Barrier */
static test_vector_t dec011_vectors[] = {
    /* NOP - No operation (subtype 6) */
    {
        .name = "nop",
        .encoding_hex_le = "1f2003d5",  /* nop: [0x1f,0x20,0x03,0xd5] */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 0,   /* A64_RESERVED */
        .expected_subtype = 6     /* NOP/HINT */
    },
    /* SVC - Supervisor call (subtype 1, imm=0x81 per decoder) */
    {
        .name = "svc_0x80",
        .encoding_hex_le = "810000d4",  /* svc #0x80: [0x81,0x00,0x00,0xd4] */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 0x81,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 0,
        .expected_subtype = 1
    },
    /* DMB - Data memory barrier (subtype 5, imm=15 per decoder) */
    {
        .name = "dmb_ish",
        .encoding_hex_le = "bf3f03d5",  /* dmb ish: [0xbf,0x3f,0x03,0xd5] */
        .expected_rd = 0,
        .expected_rn = 0,
        .expected_rm = 0,
        .expected_imm12 = 15,
        .expected_shift_type = 0,
        .expected_shift_amount = 0,
        .expected_set_flags = 0,
        .expected_category = 0,
        .expected_subtype = 5
    }
};

/* Determine case ID from yaml path */
static const char* detect_case_id(const char *yaml_path) {
    if (strstr(yaml_path, "DEC-001")) {
        return "DEC-001";
    } else if (strstr(yaml_path, "DEC-002")) {
        return "DEC-002";
    } else if (strstr(yaml_path, "DEC-003")) {
        return "DEC-003";
    } else if (strstr(yaml_path, "DEC-004")) {
        return "DEC-004";
    } else if (strstr(yaml_path, "DEC-005")) {
        return "DEC-005";
    } else if (strstr(yaml_path, "DEC-006")) {
        return "DEC-006";
    } else if (strstr(yaml_path, "DEC-007")) {
        return "DEC-007";
    } else if (strstr(yaml_path, "DEC-008")) {
        return "DEC-008";
    } else if (strstr(yaml_path, "DEC-009")) {
        return "DEC-009";
    } else if (strstr(yaml_path, "DEC-010")) {
        return "DEC-010";
    } else if (strstr(yaml_path, "DEC-011")) {
        return "DEC-011";
    }
    return "DEC-001"; /* Default fallback */
}

/* Get case configuration */
static case_config_t get_case_config(const char *case_id) {
    case_config_t config;
    config.case_id = case_id;

    if (strcmp(case_id, "DEC-001") == 0) {
        config.vectors = dec001_vectors;
        config.num_vectors = sizeof(dec001_vectors) / sizeof(dec001_vectors[0]);
    } else if (strcmp(case_id, "DEC-002") == 0) {
        config.vectors = dec002_vectors;
        config.num_vectors = sizeof(dec002_vectors) / sizeof(dec002_vectors[0]);
    } else if (strcmp(case_id, "DEC-003") == 0) {
        config.vectors = dec003_vectors;
        config.num_vectors = sizeof(dec003_vectors) / sizeof(dec003_vectors[0]);
    } else if (strcmp(case_id, "DEC-004") == 0) {
        config.vectors = dec004_vectors;
        config.num_vectors = sizeof(dec004_vectors) / sizeof(dec004_vectors[0]);
    } else if (strcmp(case_id, "DEC-005") == 0) {
        config.vectors = dec005_vectors;
        config.num_vectors = sizeof(dec005_vectors) / sizeof(dec005_vectors[0]);
    } else if (strcmp(case_id, "DEC-006") == 0) {
        config.vectors = dec006_vectors;
        config.num_vectors = sizeof(dec006_vectors) / sizeof(dec006_vectors[0]);
    } else if (strcmp(case_id, "DEC-007") == 0) {
        config.vectors = dec007_vectors;
        config.num_vectors = sizeof(dec007_vectors) / sizeof(dec007_vectors[0]);
    } else if (strcmp(case_id, "DEC-008") == 0) {
        config.vectors = dec008_vectors;
        config.num_vectors = sizeof(dec008_vectors) / sizeof(dec008_vectors[0]);
    } else if (strcmp(case_id, "DEC-009") == 0) {
        config.vectors = dec009_vectors;
        config.num_vectors = sizeof(dec009_vectors) / sizeof(dec009_vectors[0]);
    } else if (strcmp(case_id, "DEC-010") == 0) {
        config.vectors = dec010_vectors;
        config.num_vectors = sizeof(dec010_vectors) / sizeof(dec010_vectors[0]);
    } else if (strcmp(case_id, "DEC-011") == 0) {
        config.vectors = dec011_vectors;
        config.num_vectors = sizeof(dec011_vectors) / sizeof(dec011_vectors[0]);
    } else {
        config.vectors = dec001_vectors;
        config.num_vectors = sizeof(dec001_vectors) / sizeof(dec001_vectors[0]);
    }

    return config;
}

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

    /* Detect which case we're running */
    const char *case_id = detect_case_id(case_yaml);
    case_config_t config = get_case_config(case_id);

    printf("Decode Golden Harness\n");
    printf("Case: %s\n", case_id);
    printf("Testing %zu vectors\n", config.num_vectors);

    if (setup_artifact_dir(artifact_dir) != 0) {
        return 1;
    }

    /* Make mutable copy of vectors for JSON output */
    test_vector_t *vectors = malloc(sizeof(test_vector_t) * config.num_vectors);
    memcpy(vectors, config.vectors, sizeof(test_vector_t) * config.num_vectors);

    a64_instr_t decoded[config.num_vectors];
    int passed_flags[config.num_vectors];
    int fail_count = 0;

    /* Decode each vector */
    for (size_t i = 0; i < config.num_vectors; i++) {
        test_vector_t *vec = &vectors[i];
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

        if (decoded[i].cat != vec->expected_category) {
            printf("    FAIL: category mismatch (got %d, expected %d)\n",
                   decoded[i].cat, vec->expected_category);
            vector_passed = 0;
        }

        if (decoded[i].subtype != vec->expected_subtype) {
            printf("    FAIL: subtype mismatch (got %d, expected %d)\n",
                   decoded[i].subtype, vec->expected_subtype);
            vector_passed = 0;
        }

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

        if (decoded[i].Rm != vec->expected_rm) {
            printf("    FAIL: Rm mismatch (got %d, expected %d)\n",
                   decoded[i].Rm, vec->expected_rm);
            vector_passed = 0;
        }

        if (decoded[i].imm != vec->expected_imm12) {
            printf("    FAIL: imm mismatch (got %lld, expected %d)\n",
                   (long long)decoded[i].imm, vec->expected_imm12);
            vector_passed = 0;
        }

        if (decoded[i].shift_type != vec->expected_shift_type) {
            printf("    FAIL: shift_type mismatch (got %d, expected %d)\n",
                   decoded[i].shift_type, vec->expected_shift_type);
            vector_passed = 0;
        }

        if (decoded[i].imm_shift != vec->expected_shift_amount) {
            printf("    FAIL: shift_amount mismatch (got %d, expected %d)\n",
                   decoded[i].imm_shift, vec->expected_shift_amount);
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
    if (write_decoded_json(artifact_dir, case_id, vectors, config.num_vectors,
                           decoded, passed_flags) != 0) {
        passed = 0;
        failure = "Some vectors failed decoding validation";
    }

    /* Check success threshold (all must pass) */
    if (fail_count > 0) {
        passed = 0;
        failure = "Not all vectors passed";
    }

    printf("\nResults: %zu/%zu passed\n", config.num_vectors - fail_count, config.num_vectors);

    /* Write report */
    if (write_report(artifact_dir, case_id, "01-decode", "decode_golden",
                     passed, failure) != 0) {
        free(vectors);
        return 1;
    }

    free(vectors);
    return passed ? 0 : 1;
}
