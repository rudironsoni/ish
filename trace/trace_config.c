/*
 * trace_config.c
 * Environment-based configuration parsing for tracing subsystem.
 */

#include "trace/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/* Parse comma-separated event list into event mask */
static uint64_t parse_event_mask(const char *events_str)
{
    uint64_t mask = 0;
    char *copy = strdup(events_str);
    if (!copy)
        return 0;

    char *token = strtok(copy, ",");
    while (token) {
        /* Trim whitespace */
        while (*token == ' ' || *token == '\t')
            token++;

        /* Parse event name */
        if (strcmp(token, "all") == 0) {
            mask = ~0ULL;
            break;
        } else if (strcmp(token, "block.compile") == 0) {
            mask |= (1ULL << TRACE_EVENT_BLOCK_COMPILE_START);
            mask |= (1ULL << TRACE_EVENT_BLOCK_COMPILE_END);
        } else if (strcmp(token, "block.entry") == 0) {
            mask |= (1ULL << TRACE_EVENT_BLOCK_ENTRY);
        } else if (strcmp(token, "block.exit") == 0) {
            mask |= (1ULL << TRACE_EVENT_BLOCK_EXIT);
        } else if (strcmp(token, "block.cache") == 0) {
            mask |= (1ULL << TRACE_EVENT_BLOCK_CACHE_HIT);
            mask |= (1ULL << TRACE_EVENT_BLOCK_CACHE_MISS);
        } else if (strcmp(token, "fault") == 0) {
            mask |= (1ULL << TRACE_EVENT_FAULT);
        } else if (strcmp(token, "syscall") == 0) {
            mask |= (1ULL << TRACE_EVENT_SYSCALL_ENTER);
            mask |= (1ULL << TRACE_EVENT_SYSCALL_RETURN);
        } else if (strcmp(token, "decode") == 0) {
            mask |= (1ULL << TRACE_EVENT_DECODE_FAILURE);
            mask |= (1ULL << TRACE_EVENT_UNSUPPORTED_INSTRUCTION);
        } else if (strcmp(token, "register") == 0) {
            mask |= (1ULL << TRACE_EVENT_REGISTER_SNAPSHOT);
            mask |= (1ULL << TRACE_EVENT_PSTATE_SNAPSHOT);
        } else {
            /* Try to parse as specific event name */
            for (int i = 0; i < TRACE_EVENT_MAX; i++) {
                const char *name = trace_event_name((trace_event_id_t)i);
                if (name && strcasecmp(token, name) == 0) {
                    mask |= (1ULL << i);
                    break;
                }
            }
        }

        token = strtok(NULL, ",");
    }

    free(copy);
    return mask;
}

/* Parse register list (e.g., "x0,x1,x2" or "0,1,2") into mask */
static uint32_t parse_regs_mask(const char *regs_str)
{
    uint32_t mask = 0;
    char *copy = strdup(regs_str);
    if (!copy)
        return 0;

    char *token = strtok(copy, ",");
    while (token) {
        /* Trim whitespace */
        while (*token == ' ' || *token == '\t')
            token++;

        int reg = -1;

        /* Try "xN" format */
        if (token[0] == 'x' || token[0] == 'X') {
            reg = atoi(token + 1);
        } else if (token[0] == 's' || token[0] == 'S') {
            /* SP register */
            if (strcasecmp(token, "sp") == 0) {
                reg = 31;
            }
        } else {
            /* Try numeric */
            reg = atoi(token);
        }

        if (reg >= 0 && reg <= 31) {
            mask |= (1U << reg);
        }

        token = strtok(NULL, ",");
    }

    free(copy);
    return mask;
}

/* Parse PC range (e.g., "0xf7fb0100-0xf7fb0200") */
static int parse_pc_range(const char *range_str, uint64_t *start, uint64_t *end)
{
    char *copy = strdup(range_str);
    if (!copy)
        return -1;

    char *dash = strchr(copy, '-');
    if (!dash) {
        free(copy);
        return -1;
    }

    *dash = '\0';
    char *start_str = copy;
    char *end_str = dash + 1;

    /* Trim whitespace */
    while (*start_str == ' ' || *start_str == '\t')
        start_str++;
    while (*end_str == ' ' || *end_str == '\t')
        end_str++;

    /* Parse hex or decimal */
    if (start_str[0] == '0' && (start_str[1] == 'x' || start_str[1] == 'X')) {
        sscanf(start_str, "%llx", (unsigned long long *)start);
    } else {
        *start = (uint64_t)atoll(start_str);
    }

    if (end_str[0] == '0' && (end_str[1] == 'x' || end_str[1] == 'X')) {
        sscanf(end_str, "%llx", (unsigned long long *)end);
    } else {
        *end = (uint64_t)atoll(end_str);
    }

    free(copy);
    return 0;
}

/* Parse configuration from environment variables */
int trace_config_from_env(trace_config_t *config)
{
    if (!config) {
        return -1;
    }

    /* Initialize with defaults */
    memset(config, 0, sizeof(trace_config_t));
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_SIMULATOR)
    config->backend = TRACE_BACKEND_OS_LOG; /* Default to os_log on iOS */
#else
    config->backend = TRACE_BACKEND_STDERR; /* Default to stderr elsewhere */
#endif
    config->level = TRACE_LEVEL_SUMMARY; /* Default enabled at summary level */
    config->ring_size = 16384;           /* Default ring size */
    config->category_mask = 0xFF;        /* Enable all categories by default */
    config->event_mask = ~0ULL;          /* Enable all events by default */

    /* ISH_TRACE_BACKEND */
    const char *backend = getenv("ISH_TRACE_BACKEND");
    if (backend) {
        if (strcmp(backend, "nop") == 0) {
            config->backend = TRACE_BACKEND_NOP;
        } else if (strcmp(backend, "ring") == 0) {
            config->backend = TRACE_BACKEND_RING;
        } else if (strcmp(backend, "stderr") == 0) {
            config->backend = TRACE_BACKEND_STDERR;
        } else if (strcmp(backend, "oslog") == 0 || strcmp(backend, "os_log") == 0) {
            config->backend = TRACE_BACKEND_OS_LOG;
        }
    }

    /* ISH_TRACE_LEVEL */
    const char *level = getenv("ISH_TRACE_LEVEL");
    if (level) {
        int lvl = atoi(level);
        if (lvl >= 0 && lvl <= 5) {
            config->level = (trace_level_t)lvl;
        }
    }

    /* ISH_TRACE_EVENTS - comma-separated event names */
    const char *events = getenv("ISH_TRACE_EVENTS");
    if (events) {
        config->event_mask = parse_event_mask(events);
    } else {
        /* Default: enable all events at or below configured level */
        config->event_mask = ~0ULL;
    }

    /* ISH_TRACE_PC - PC range filter */
    const char *pc_range = getenv("ISH_TRACE_PC");
    if (pc_range) {
        parse_pc_range(pc_range, &config->pc_start, &config->pc_end);
    }

    /* ISH_TRACE_REGS - register filter for snapshots */
    const char *regs = getenv("ISH_TRACE_REGS");
    if (regs) {
        config->regs_mask = parse_regs_mask(regs);
    } else {
        /* Default: snapshot x0-x5 */
        config->regs_mask = 0x3F;
    }

    /* ISH_TRACE_RING_SIZE */
    const char *ring_size = getenv("ISH_TRACE_RING_SIZE");
    if (ring_size) {
        long size = atol(ring_size);
        if (size > 0) {
            config->ring_size = (size_t)size;
        }
    }

    /* ISH_TRACE_OUT - output file path */
    const char *out_path = getenv("ISH_TRACE_OUT");
    if (out_path) {
        strncpy(config->output_path, out_path, sizeof(config->output_path) - 1);
        config->output_path[sizeof(config->output_path) - 1] = '\0';
    }

    /* ISH_TRACE_DUMP_ON - dump triggers */
    const char *dump_on = getenv("ISH_TRACE_DUMP_ON");
    if (dump_on) {
        if (strcmp(dump_on, "fault") == 0 || strcmp(dump_on, "all") == 0) {
            config->dump_on_fault = true;
        }
    }

    /* ISH_TRACE_MAX_EVENTS */
    const char *max_events = getenv("ISH_TRACE_MAX_EVENTS");
    if (max_events) {
        config->max_events = (uint64_t)atoll(max_events);
    }

    return 0;
}

/* Print current configuration to stderr (for debugging) */
void trace_config_print(trace_config_t *config)
{
    fprintf(stderr, "[TRACE] Configuration:\n");

    fprintf(stderr, "  Backend: ");
    switch (config->backend) {
    case TRACE_BACKEND_NOP:
        fprintf(stderr, "nop\n");
        break;
    case TRACE_BACKEND_RING:
        fprintf(stderr, "ring\n");
        break;
    case TRACE_BACKEND_STDERR:
        fprintf(stderr, "stderr\n");
        break;
    default:
        fprintf(stderr, "unknown\n");
        break;
    }

    fprintf(stderr, "  Level: %d\n", config->level);
    fprintf(stderr, "  Event mask: 0x%016llx\n", (unsigned long long)config->event_mask);
    fprintf(stderr, "  Category mask: 0x%02x\n", config->category_mask);
    fprintf(stderr, "  PC range: 0x%016llx - 0x%016llx\n", (unsigned long long)config->pc_start,
            (unsigned long long)config->pc_end);
    fprintf(stderr, "  Register mask: 0x%08x\n", config->regs_mask);
    fprintf(stderr, "  Ring size: %zu\n", config->ring_size);
    fprintf(stderr, "  Output path: %s\n", config->output_path[0] ? config->output_path : "(none)");
    fprintf(stderr, "  Dump on fault: %s\n", config->dump_on_fault ? "yes" : "no");
    fprintf(stderr, "  Max events: %llu\n", (unsigned long long)config->max_events);
}
