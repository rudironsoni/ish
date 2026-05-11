// GuestExecutionTraceSink.c
// Test-only trace sink for capturing guest exit events
// Owner: Tests/IXLandLinuxRuntimeSystemTests/Support/GuestExecutionTrace
// Uses ixland_instrumentation_register_sink() to observe events externally

#include "GuestExecutionTraceSink.h"
#include "GuestExecutionProbe.h"

#include <IXLandInstrumentation/IXLandInstrumentation.h>
#include <Block.h>
#include <os/lock.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Static state for exit observation
// These MUST NOT be __thread - callback is set from XCTest thread but invoked from guest thread
// exit_event_received and last_exit_code are checked from both threads
static bool sink_registered = false;
static bool exit_event_received = false;
static int last_exit_code = -1;
static guest_execution_trace_sink_callback_t completion_callback = NULL;
static os_unfair_lock sink_state_lock = OS_UNFAIR_LOCK_INIT;

static void invoke_completion_callback(bool exit_observed, int exit_code)
{
    guest_execution_trace_sink_callback_t callback = NULL;
    os_unfair_lock_lock(&sink_state_lock);
    callback = completion_callback;
    os_unfair_lock_unlock(&sink_state_lock);

    if (callback != NULL) {
        callback(exit_observed, exit_code);
    }
}

// S0: Sink Diagnostic - proves callbacks are being invoked
static uint64_t begin_interval_calls_count = 0;
static uint64_t end_interval_calls_count = 0;
static bool any_interval_received = false;

// Milestone B: Dynamic ELF loader state
static bool interp_path_resolved = false;
static bool elf_exec_reached = false;
static bool main_elf_header_accepted = false;
static bool interp_header_loaded = false;
static bool interp_mappings_exist = false;
static bool main_image_loaded = false;
static bool auxv_initialized = false;
static char resolved_interp_path[256] = { 0 };
static char last_loader_event[256] = { 0 };

// D2.0: Interp open state tracking
static bool interp_open_attempted = false;
static bool interp_open_succeeded = false;
static int interp_open_errno = 0;

// Pre-elf_exec diagnostic ladder (X0-X3)
static bool do_execve_entered = false;
static bool format_exec_entered = false;
static bool before_elf_exec_entered = false; // X2
static bool elf_exec_entered = false;        // X3 (elf_exec returned)

// --- New proof event storage (protected by sink_state_lock) ---
static bool ldrh_6d1c0_seen = false;
static uint64_t ldrh_6d1c0_addr = 0;
static uint16_t ldrh_6d1c0_val = 0;
static int ldrh_6d1c0_mem_ret = 0;
static uint64_t ldrh_6d1c0_host_ptr = 0;

static bool wb_6d1c0_seen = false;
static uint64_t wb_6d1c0_x0_after = 0;
static uint64_t wb_6d1c0_value = 0;
static unsigned long long wb_6d1c0_rt = 0;
static unsigned long long wb_6d1c0_size = 0;
static int wb_6d1c0_is_64bit = 0;

#define COMPILE_PC_TABLE_SIZE 4096
#define COMPILE_PC_GENERATION_SLOTS 8
struct compile_pc_entry {
    uint64_t mmu;
    uint64_t pc;
    uint64_t compile_count;
    uint64_t l0_hit_count;
    uint64_t l1_hit_count;
    uint64_t insert_count;
    uint64_t compile_failure_count;
    uint64_t miss_not_found_count;
    uint64_t miss_jetsam_count;
    uint64_t miss_generation_count;
    uint64_t generations[COMPILE_PC_GENERATION_SLOTS];
    uint32_t generation_count;
    bool multigeneration;
    bool used;
};
static struct compile_pc_entry compile_pc_table[COMPILE_PC_TABLE_SIZE];
static uint64_t compile_entry_count = 0;
static uint64_t compile_unique_pc_count = 0;
static uint64_t compile_multigeneration_pc_count = 0;
static uint64_t compile_max_count_per_pc = 0;
static uint64_t compile_max_generation_count_per_pc = 0;
static uint64_t compile_hottest_mmu = 0;
static uint64_t compile_hottest_pc = 0;
static uint64_t compile_hottest_generation_count = 0;
static uint64_t cache_alloc_success_count = 0;
static uint64_t cache_alloc_failure_count = 0;
static uint64_t exec_entry_count = 0;
static uint64_t exec_hottest_pc = 0;
static uint64_t exec_hottest_count = 0;
static uint64_t root_stat_attempt_count = 0;
static uint64_t root_stat_ok_count = 0;
static uint64_t root_stat_fail_count = 0;
static uint64_t stdout_header_write_count = 0;
static uint64_t stdout_metadata_write_count = 0;
static uint64_t stdout_prompt_write_count = 0;
static uint64_t pty_header_write_count = 0;
static uint64_t pty_metadata_write_count = 0;
static uint64_t pty_prompt_write_count = 0;
static bool uname_syscall_entered = false;
static bool uname_syscall_returned = false;
static uint64_t uname_syscall_return_value = 0;
static bool stdout_aarch64_write_observed = false;
static bool pty_aarch64_write_observed = false;

static size_t compile_pc_slot(uint64_t mmu, uint64_t pc);

static struct compile_pc_entry *lookup_compile_entry(uint64_t mmu, uint64_t pc, bool create)
{
    size_t slot = compile_pc_slot(mmu, pc);
    for (size_t probe = 0; probe < COMPILE_PC_TABLE_SIZE; probe++) {
        struct compile_pc_entry *candidate =
            &compile_pc_table[(slot + probe) & (COMPILE_PC_TABLE_SIZE - 1)];
        if (!candidate->used) {
            if (!create)
                return NULL;
            candidate->used = true;
            candidate->mmu = mmu;
            candidate->pc = pc;
            compile_unique_pc_count++;
            return candidate;
        }
        if (candidate->mmu == mmu && candidate->pc == pc)
            return candidate;
    }
    return NULL;
}

static void refresh_compile_hottest(const struct compile_pc_entry *entry)
{
    if (!entry)
        return;
    if (compile_hottest_mmu == entry->mmu && compile_hottest_pc == entry->pc)
        compile_hottest_generation_count = entry->generation_count;
    if (compile_max_count_per_pc < entry->compile_count) {
        compile_max_count_per_pc = entry->compile_count;
        compile_hottest_mmu = entry->mmu;
        compile_hottest_pc = entry->pc;
        compile_hottest_generation_count = entry->generation_count;
    }
}

// Simple fixed-size table for x0 mutation observations
#define X0_MUTATION_TABLE_SIZE 32
struct x0_mutation_entry {
    uint64_t pc;
    uint64_t new_x0;
    uint64_t old_x0;
    uint64_t value;
    unsigned long long size;
    int is_64bit;
    int used;
};
static struct x0_mutation_entry x0_mutations[X0_MUTATION_TABLE_SIZE];

// Forward declaration
static void test_sink_record_event(ixland_instrumentation_origin_t origin, const char *event_name);
static uint64_t test_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count);
static void test_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count);
static bool test_sink_is_active(void);
static void noop_sink_record_event(ixland_instrumentation_origin_t origin, const char *event_name);
static uint64_t noop_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count);
static void noop_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count);
static bool noop_sink_is_active(void);

static size_t compile_pc_slot(uint64_t mmu, uint64_t pc)
{
    return (size_t)((((mmu >> 4) ^ (pc >> 2) ^ (pc >> 13))) & (COMPILE_PC_TABLE_SIZE - 1));
}

static void record_compile_entry(uint64_t mmu, uint64_t pc, uint64_t mmu_gen)
{
    compile_entry_count++;

    struct compile_pc_entry *entry = lookup_compile_entry(mmu, pc, true);

    if (entry == NULL)
        return;

    if (entry->generation_count == 0) {
        entry->generation_count = 1;
        entry->generations[0] = mmu_gen;
        if (compile_max_generation_count_per_pc < 1)
            compile_max_generation_count_per_pc = 1;
    }

    entry->compile_count++;
    refresh_compile_hottest(entry);

    for (uint32_t i = 0; i < entry->generation_count; i++) {
        if (entry->generations[i] == mmu_gen)
            return;
    }

    if (entry->generation_count < COMPILE_PC_GENERATION_SLOTS)
        entry->generations[entry->generation_count] = mmu_gen;
    entry->generation_count++;
    if (compile_max_generation_count_per_pc < entry->generation_count)
        compile_max_generation_count_per_pc = entry->generation_count;
    if (compile_hottest_mmu == entry->mmu && compile_hottest_pc == entry->pc)
        compile_hottest_generation_count = entry->generation_count;
    if (!entry->multigeneration) {
        entry->multigeneration = true;
        compile_multigeneration_pc_count++;
    }
}

static void record_cache_hit(uint64_t mmu, uint64_t pc, const char *level)
{
    struct compile_pc_entry *entry = lookup_compile_entry(mmu, pc, true);
    if (!entry)
        return;
    if (level && strcmp(level, "l0") == 0)
        entry->l0_hit_count++;
    else if (level && strcmp(level, "l1") == 0)
        entry->l1_hit_count++;
}

static void record_cache_insert(uint64_t mmu, uint64_t pc)
{
    struct compile_pc_entry *entry = lookup_compile_entry(mmu, pc, true);
    if (!entry)
        return;
    entry->insert_count++;
}

static void record_compile_failure(uint64_t mmu, uint64_t pc)
{
    struct compile_pc_entry *entry = lookup_compile_entry(mmu, pc, true);
    if (!entry)
        return;
    entry->compile_failure_count++;
}

static void record_cache_miss(uint64_t mmu, uint64_t pc, const char *reason)
{
    struct compile_pc_entry *entry = lookup_compile_entry(mmu, pc, true);
    if (!entry || !reason)
        return;
    if (strcmp(reason, "not_found") == 0)
        entry->miss_not_found_count++;
    else if (strcmp(reason, "jetsam") == 0)
        entry->miss_jetsam_count++;
    else if (strcmp(reason, "generation") == 0)
        entry->miss_generation_count++;
}

#define EXEC_PC_TABLE_SIZE 4096
struct exec_pc_entry {
    uint64_t pc;
    uint64_t exec_count;
    bool used;
};
static struct exec_pc_entry exec_pc_table[EXEC_PC_TABLE_SIZE];

static size_t exec_pc_slot(uint64_t pc)
{
    return (size_t)(((pc >> 2) ^ (pc >> 13)) & (EXEC_PC_TABLE_SIZE - 1));
}

static void record_exec_entry(uint64_t pc)
{
    exec_entry_count++;

    size_t slot = exec_pc_slot(pc);
    for (size_t probe = 0; probe < EXEC_PC_TABLE_SIZE; probe++) {
        struct exec_pc_entry *entry = &exec_pc_table[(slot + probe) & (EXEC_PC_TABLE_SIZE - 1)];
        if (!entry->used) {
            entry->used = true;
            entry->pc = pc;
            entry->exec_count = 1;
            if (exec_hottest_count < entry->exec_count) {
                exec_hottest_pc = pc;
                exec_hottest_count = entry->exec_count;
            }
            return;
        }
        if (entry->pc == pc) {
            entry->exec_count++;
            if (exec_hottest_count < entry->exec_count) {
                exec_hottest_pc = pc;
                exec_hottest_count = entry->exec_count;
            }
            return;
        }
    }
}

// Test sink implementation - captures events from runtime
static ixland_instrumentation_sink_t test_sink = { .bootstrap = NULL,
                                                   .activate = NULL,
                                                   .is_active = test_sink_is_active,
                                                   .record_event = test_sink_record_event,
                                                   .begin_interval = test_sink_begin_interval,
                                                   .end_interval = test_sink_end_interval };

static ixland_instrumentation_sink_t noop_active_sink = { .bootstrap = NULL,
                                                          .activate = NULL,
                                                          .is_active = noop_sink_is_active,
                                                          .record_event = noop_sink_record_event,
                                                          .begin_interval = noop_sink_begin_interval,
                                                          .end_interval = noop_sink_end_interval };

static void test_sink_record_event(ixland_instrumentation_origin_t origin, const char *event_name)
{
    (void)origin;
    if (!event_name)
        return;

    if (strncmp(event_name, "tcti.compile.entry=pc:", 22) == 0) {
        unsigned long long mmu = 0ULL;
        unsigned long long pc = 0ULL;
        unsigned long long mmu_gen = 0ULL;
        if (sscanf(event_name,
                   "tcti.compile.entry=pc:0x%llx,tlb:%*d,mmu:0x%llx,mmu_gen:%*llu,code_gen:%llu",
                   &pc, &mmu, &mmu_gen) == 3 ||
            sscanf(event_name,
                   "tcti.compile.entry=pc:0x%llx,tlb:%*d,mmu:0x%llx,mmu_gen:%llu", &pc, &mmu,
                   &mmu_gen) == 3) {
            os_unfair_lock_lock(&sink_state_lock);
            record_compile_entry((uint64_t)mmu, (uint64_t)pc, (uint64_t)mmu_gen);
            os_unfair_lock_unlock(&sink_state_lock);
        }
        return;
    }

    if (strncmp(event_name, "tcti.cache.hit=pc:", 18) == 0) {
        unsigned long long mmu = 0ULL;
        unsigned long long pc = 0ULL;
        char level[8] = { 0 };
        if (sscanf(event_name, "tcti.cache.hit=pc:0x%llx,mmu:0x%llx,level:%7[^,]", &pc, &mmu,
                   level) == 3) {
            os_unfair_lock_lock(&sink_state_lock);
            record_cache_hit((uint64_t)mmu, (uint64_t)pc, level);
            os_unfair_lock_unlock(&sink_state_lock);
        }
        return;
    }

    if (strstr(event_name, "tcti.cache.insert=pc:") != NULL) {
        unsigned long long mmu = 0ULL;
        unsigned long long pc = 0ULL;
        if (sscanf(event_name, "tcti.cache.insert=pc:0x%llx,mmu:0x%llx", &pc, &mmu) == 2) {
            os_unfair_lock_lock(&sink_state_lock);
            record_cache_insert((uint64_t)mmu, (uint64_t)pc);
            os_unfair_lock_unlock(&sink_state_lock);
        }
        return;
    }

    if (strncmp(event_name, "tcti.cache.miss=pc:", 19) == 0) {
        unsigned long long mmu = 0ULL;
        unsigned long long pc = 0ULL;
        char reason[16] = { 0 };
        if (sscanf(event_name, "tcti.cache.miss=pc:0x%llx,mmu:0x%llx,reason:%15[^,]", &pc, &mmu,
                   reason) == 3) {
            os_unfair_lock_lock(&sink_state_lock);
            record_cache_miss((uint64_t)mmu, (uint64_t)pc, reason);
            os_unfair_lock_unlock(&sink_state_lock);
        }
        return;
    }

    if (strncmp(event_name, "tcti.compile.fail=pc:", 21) == 0) {
        unsigned long long mmu = 0ULL;
        unsigned long long pc = 0ULL;
        if (sscanf(event_name, "tcti.compile.fail=pc:0x%llx,mmu:0x%llx", &pc, &mmu) == 2) {
            os_unfair_lock_lock(&sink_state_lock);
            record_compile_failure((uint64_t)mmu, (uint64_t)pc);
            os_unfair_lock_unlock(&sink_state_lock);
        }
        return;
    }

    if (strncmp(event_name, "tcti.dispatch.lookup=pc:", 24) == 0) {
        unsigned long long start_pc = 0ULL;
        int block_present = 0;
        if (sscanf(event_name,
                   "tcti.dispatch.lookup=pc:%*llx,block:%d,start:0x%llx,end:%*llx,gadgets:%*zu,"
                   "explicit:%*d,total_before:%*d",
                   &block_present, &start_pc) == 2 &&
            block_present == 1) {
            os_unfair_lock_lock(&sink_state_lock);
            record_exec_entry((uint64_t)start_pc);
            os_unfair_lock_unlock(&sink_state_lock);
        }
        return;
    }

    if (strncmp(event_name, "tcti.cache.alloc=mmu:", 21) == 0) {
        unsigned long long mmu = 0ULL;
        int ok = 0;
        if (sscanf(event_name, "tcti.cache.alloc=mmu:0x%llx,ok:%d", &mmu, &ok) == 2) {
            (void)mmu;
            os_unfair_lock_lock(&sink_state_lock);
            if (ok)
                cache_alloc_success_count++;
            else
                cache_alloc_failure_count++;
            os_unfair_lock_unlock(&sink_state_lock);
        }
        return;
    }

    if (strncmp(event_name, "a64.newfstatat.root.attempt", 27) == 0 ||
        strncmp(event_name, "statx.root.attempt", 18) == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        root_stat_attempt_count++;
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    if (strncmp(event_name, "a64.newfstatat.root.ok", 22) == 0 ||
        strncmp(event_name, "statx.root.ok", 13) == 0 ||
        strncmp(event_name, "a64.fstat.root.ok", 17) == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        root_stat_ok_count++;
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    if (strncmp(event_name, "a64.newfstatat.root.fail", 24) == 0 ||
        strncmp(event_name, "statx.root.fail", 15) == 0 ||
        strncmp(event_name, "fstat.rootdir.fail", 18) == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        root_stat_fail_count++;
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // Preserve all prior milestone and diagnostic event handling, then merge in the
    // structured proof parsing implemented earlier. Maintain lock protection for
    // shared mutable sink state.

    // Capture guest.do_exit_group.entry event (Milestones A/B)
    if (strcmp(event_name, "guest.do_exit_group.entry") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        exit_event_received = true;
        os_unfair_lock_unlock(&sink_state_lock);
        probe_task_exit_observed(last_exit_code);
        return;
    }
    // Capture guest.do_exit.entry
    if (strcmp(event_name, "guest.do_exit.entry") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        exit_event_received = true;
        os_unfair_lock_unlock(&sink_state_lock);
        probe_task_exit_observed(last_exit_code);
        return;
    }

    // Milestone B: Interp path resolved (only if it's a REAL path, not "none")
    if (strstr(event_name, "loader.interpreter_path=path:") != NULL) {
        const char *path_start = strstr(event_name, "path:");
        if (path_start) {
            path_start += 5; // Skip "path:"
            // Only count if it's not "none" - real dynamic ELF must have a real interpreter
            if (strncmp(path_start, "none", 4) != 0) {
                os_unfair_lock_lock(&sink_state_lock);
                interp_path_resolved = true;
                snprintf(resolved_interp_path, sizeof(resolved_interp_path), "%s", path_start);
                snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
                os_unfair_lock_unlock(&sink_state_lock);
                // Invoke callback for loader boundary - interp path resolved
                invoke_completion_callback(false, -1);
            }
        }
        return;
    }

    // DIAGNOSTIC: format_exec was called
    if (strstr(event_name, "loader.format_exec.called") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // DIAGNOSTIC: elf_exec was reached
    if (strstr(event_name, "loader.elf_exec.reached") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        elf_exec_reached = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // M1: Main ELF header accepted (emitted after read_header succeeds for main binary)
    if (strstr(event_name, "loader.main_elf.header") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        main_elf_header_accepted = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // Milestone B: Interp pt_load mapping
    if (strstr(event_name, "loader.interp.pt_load.map") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        interp_mappings_exist = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // Milestone B: Auxv initialized with AT_BASE
    if (strstr(event_name, "loader.auxv.at_base.write") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        auxv_initialized = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // D2.0: Interp open result (emitted at exec.c after generic_open attempt)
    if (strstr(event_name, "loader.interp.open.result") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        interp_open_attempted = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        // Parse err:X from event to classify open result
        const char *err_str = strstr(event_name, "err:");
        if (err_str) {
            int err_val = atoi(err_str + 4);
            if (err_val == 0) {
                interp_open_succeeded = true;
                interp_open_errno = 0;
            } else {
                interp_open_succeeded = false;
                interp_open_errno = err_val;
            }
        }
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // Milestone B: Interp header loaded (emitted after read_header succeeds)
    if (strstr(event_name, "loader.interp_elf.header") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        interp_header_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // Milestone B: Interp bias compute (emitted during interpreter mapping phase)
    if (strstr(event_name, "loader.interp.bias.compute") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        interp_header_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // Milestone B: Main image loaded
    if (strstr(event_name, "task.proof.exec.load_entry.reached") != NULL) {
        os_unfair_lock_lock(&sink_state_lock);
        main_image_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        os_unfair_lock_unlock(&sink_state_lock);
        return;
    }

    // PROOF events: parse structured proof lines (robust parsing, keep prior semantics intact)
    if (strstr(event_name, "task.proof.6d1c0.ldrh_result=") != NULL) {
        const char *p = strstr(event_name, "addr:");
        if (p) {
            unsigned long long tmp_addr = 0ULL;
            unsigned long long tmp_host_ptr = 0ULL;
            unsigned int read_val = 0;
            int mem_ret = 0;
            int sscanf_ret = sscanf(p, "addr:0x%llx,read_val:0x%04x,mem_ret:%d,host_ptr:0x%llx",
                                    &tmp_addr, &read_val, &mem_ret, &tmp_host_ptr);
            if (sscanf_ret >= 4) {
                os_unfair_lock_lock(&sink_state_lock);
                ldrh_6d1c0_seen = true;
                ldrh_6d1c0_addr = (uint64_t)tmp_addr;
                ldrh_6d1c0_val = (uint16_t)read_val;
                ldrh_6d1c0_mem_ret = mem_ret;
                ldrh_6d1c0_host_ptr = (uint64_t)tmp_host_ptr;
                os_unfair_lock_unlock(&sink_state_lock);
            }
        }
        return;
    }

    if (strstr(event_name, "task.proof.6d1c0.writeback=") != NULL) {
        const char *p = strstr(event_name, "x0_after_write:");
        if (p) {
            unsigned long long x0_after = 0ULL, value = 0ULL, rt_tmp = 0ULL, size_tmp = 0ULL;
            int is64 = 0;
            int sscanf_ret2 = sscanf(p, "x0_after_write:0x%llx,value:0x%llx,rt:%llu,size:%llu,is_64bit:%d",
                                     &x0_after, &value, &rt_tmp, &size_tmp, &is64);
            if (sscanf_ret2 >= 5) {
                os_unfair_lock_lock(&sink_state_lock);
                wb_6d1c0_seen = true;
                wb_6d1c0_x0_after = (uint64_t)x0_after;
                wb_6d1c0_value = (uint64_t)value;
                wb_6d1c0_rt = rt_tmp;
                wb_6d1c0_size = size_tmp;
                wb_6d1c0_is_64bit = is64;
                os_unfair_lock_unlock(&sink_state_lock);
            }
        }
        return;
    }

    if (strstr(event_name, "task.proof.x0.mutation=") != NULL) {
        const char *p = strstr(event_name, "pc:");
        if (p) {
            unsigned long long pc = 0ULL, new_x0 = 0ULL, old_x0 = 0ULL, value = 0ULL, size_tmp = 0ULL;
            int is64 = 0;
            int sscanf_ret3 = sscanf(p, "pc:0x%llx,new_x0:0x%llx,old_x0:0x%llx,value:0x%llx,size:%llu,is_64bit:%d",
                                    &pc, &new_x0, &old_x0, &value, &size_tmp, &is64);
            if (sscanf_ret3 >= 6) {
                os_unfair_lock_lock(&sink_state_lock);
                int inserted = 0;
                for (int i = 0; i < X0_MUTATION_TABLE_SIZE; i++) {
                    if (!x0_mutations[i].used) {
                        x0_mutations[i].pc = (uint64_t)pc;
                        x0_mutations[i].new_x0 = (uint64_t)new_x0;
                        x0_mutations[i].old_x0 = (uint64_t)old_x0;
                        x0_mutations[i].value = (uint64_t)value;
                        x0_mutations[i].size = size_tmp;
                        x0_mutations[i].is_64bit = is64;
                        x0_mutations[i].used = 1;
                        inserted = 1;
                        break;
                    }
                }
                if (!inserted) {
                    x0_mutations[0].pc = (uint64_t)pc;
                    x0_mutations[0].new_x0 = (uint64_t)new_x0;
                    x0_mutations[0].old_x0 = (uint64_t)old_x0;
                    x0_mutations[0].value = (uint64_t)value;
                    x0_mutations[0].size = size_tmp;
                    x0_mutations[0].is_64bit = is64;
                    x0_mutations[0].used = 1;
                }
                os_unfair_lock_unlock(&sink_state_lock);
            }
        }
        return;
    }

    // Ignore unrelated high-volume events. This sink tracks explicit proof surfaces only.
}

static uint64_t test_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count)
{
    os_unfair_lock_lock(&sink_state_lock);
    begin_interval_calls_count++;
    any_interval_received = true;
    os_unfair_lock_unlock(&sink_state_lock);

    if (!interval_name)
        return 0;

    if (strcmp(interval_name, "guest.do_exit_group.entry") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        exit_event_received = true;
        last_exit_code = -1;
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "status") == 0) {
                last_exit_code = atoi(attrs[i].value);
                break;
            }
        }
        os_unfair_lock_unlock(&sink_state_lock);
        probe_task_exit_observed(last_exit_code);
        invoke_completion_callback(true, last_exit_code);
    } else if (strcmp(interval_name, "guest.do_exit.entry") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        exit_event_received = true;
        last_exit_code = -1;
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "status") == 0) {
                last_exit_code = atoi(attrs[i].value);
                break;
            }
        }
        os_unfair_lock_unlock(&sink_state_lock);
        probe_task_exit_observed(last_exit_code);
        invoke_completion_callback(true, last_exit_code);
    } else if (strcmp(interval_name, "task.proof.loader.elf_header") == 0) {
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "role") == 0 && attrs[i].value) {
                if (strcmp(attrs[i].value, "main") == 0) {
                    os_unfair_lock_lock(&sink_state_lock);
                    main_elf_header_accepted = true;
                    snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.loader.elf_header:role=main");
                    os_unfair_lock_unlock(&sink_state_lock);
                    break;
                } else if (strcmp(attrs[i].value, "interp") == 0) {
                    os_unfair_lock_lock(&sink_state_lock);
                    interp_header_loaded = true;
                    snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.loader.elf_header:role=interp");
                    os_unfair_lock_unlock(&sink_state_lock);
                    break;
                }
            }
        }
    } else if (strcmp(interval_name, "task.proof.loader.biases") == 0) {
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "interp_path") == 0 && attrs[i].value) {
                if (strncmp(attrs[i].value, "none", 4) != 0) {
                    os_unfair_lock_lock(&sink_state_lock);
                    interp_path_resolved = true;
                    snprintf(resolved_interp_path, sizeof(resolved_interp_path), "%s", attrs[i].value);
                    snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.loader.biases:interp_path_resolved");
                    os_unfair_lock_unlock(&sink_state_lock);
                    invoke_completion_callback(false, -1);
                }
                break;
            }
        }
    } else if (strcmp(interval_name, "task.proof.exec.load_entry.reached") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        main_image_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", interval_name);
        os_unfair_lock_unlock(&sink_state_lock);
    } else if (strcmp(interval_name, "task.proof.do_execve.entry") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        do_execve_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.do_execve.entry");
        os_unfair_lock_unlock(&sink_state_lock);
    } else if (strcmp(interval_name, "task.proof.do_execve.before_format_exec") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        format_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.do_execve.before_format_exec");
        os_unfair_lock_unlock(&sink_state_lock);
    } else if (strcmp(interval_name, "task.proof.do_execve.before_elf_exec") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        before_elf_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.do_execve.before_elf_exec");
        os_unfair_lock_unlock(&sink_state_lock);
    } else if (strcmp(interval_name, "task.proof.elf_exec.after_return_to_caller") == 0) {
        os_unfair_lock_lock(&sink_state_lock);
        elf_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.elf_exec.after_return_to_caller");
        os_unfair_lock_unlock(&sink_state_lock);
    } else if (strcmp(interval_name, "task.proof.guest.write.attempt") == 0) {
        const char *fd_value = NULL;
        const char *preview = NULL;
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key == NULL || attrs[i].value == NULL)
                continue;
            if (strcmp(attrs[i].key, "fd") == 0)
                fd_value = attrs[i].value;
            else if (strcmp(attrs[i].key, "preview") == 0)
                preview = attrs[i].value;
        }
        if (fd_value != NULL && preview != NULL && strcmp(fd_value, "1") == 0) {
            os_unfair_lock_lock(&sink_state_lock);
            if (strstr(preview, "total 0") != NULL)
                stdout_header_write_count++;
            if (strstr(preview, "drwx") != NULL || strstr(preview, "-rw") != NULL ||
                strstr(preview, "lrwx") != NULL)
                stdout_metadata_write_count++;
            if (strstr(preview, "/ # ") != NULL || strcmp(preview, "/ #") == 0)
                stdout_prompt_write_count++;
            if (strstr(preview, "aarch64") != NULL)
                stdout_aarch64_write_observed = true;
            os_unfair_lock_unlock(&sink_state_lock);
        }
    } else if (strcmp(interval_name, "task.proof.pty.slave.write") == 0) {
        const char *preview = NULL;
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key != NULL && attrs[i].value != NULL &&
                strcmp(attrs[i].key, "preview") == 0) {
                preview = attrs[i].value;
                break;
            }
        }
        if (preview != NULL) {
            os_unfair_lock_lock(&sink_state_lock);
            if (strstr(preview, "total 0") != NULL)
                pty_header_write_count++;
            if (strstr(preview, "drwx") != NULL || strstr(preview, "-rw") != NULL ||
                strstr(preview, "lrwx") != NULL)
                pty_metadata_write_count++;
            if (strstr(preview, "/ # ") != NULL || strcmp(preview, "/ #") == 0)
                pty_prompt_write_count++;
            if (strstr(preview, "aarch64") != NULL)
                pty_aarch64_write_observed = true;
            os_unfair_lock_unlock(&sink_state_lock);
        }
    } else if (strcmp(interval_name, "guest.syscall.enter") == 0) {
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key != NULL && attrs[i].value != NULL &&
                strcmp(attrs[i].key, "name") == 0 && strcmp(attrs[i].value, "uname") == 0) {
                os_unfair_lock_lock(&sink_state_lock);
                uname_syscall_entered = true;
                os_unfair_lock_unlock(&sink_state_lock);
                break;
            }
        }
    } else if (strcmp(interval_name, "guest.syscall.return") == 0) {
        bool is_uname = false;
        uint64_t ret_value = 0;
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key == NULL || attrs[i].value == NULL)
                continue;
            if (strcmp(attrs[i].key, "name") == 0 && strcmp(attrs[i].value, "uname") == 0)
                is_uname = true;
            else if (strcmp(attrs[i].key, "ret") == 0)
                ret_value = strtoull(attrs[i].value, NULL, 0);
        }
        if (is_uname) {
            os_unfair_lock_lock(&sink_state_lock);
            uname_syscall_returned = true;
            uname_syscall_return_value = ret_value;
            os_unfair_lock_unlock(&sink_state_lock);
        }
    }

    return 0;
}

static void test_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count)
{
    (void)interval_id;
    (void)attrs;
    (void)attr_count;
    end_interval_calls_count++;
}

static bool test_sink_is_active(void)
{
    return true;
}

static void noop_sink_record_event(ixland_instrumentation_origin_t origin, const char *event_name)
{
    (void)origin;
    (void)event_name;
}

static uint64_t noop_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count)
{
    (void)origin;
    (void)interval_name;
    (void)attrs;
    (void)attr_count;
    return 1;
}

static void noop_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count)
{
    (void)interval_id;
    (void)attrs;
    (void)attr_count;
}

static bool noop_sink_is_active(void)
{
    return true;
}

// Public API
void guest_execution_trace_sink_init(void)
{
    ixland_instrumentation_register_sink(&test_sink);
    sink_registered = true;
}

void guest_execution_trace_sink_init_noop_active(void)
{
    ixland_instrumentation_register_sink(&noop_active_sink);
    sink_registered = true;
}

bool guest_execution_trace_sink_exit_observed(void)
{
    return exit_event_received;
}

int guest_execution_trace_sink_get_exit_code(void)
{
    return last_exit_code;
}

void guest_execution_trace_sink_set_completion_callback(
    guest_execution_trace_sink_callback_t callback)
{
    os_unfair_lock_lock(&sink_state_lock);
    if (completion_callback != NULL) {
        Block_release(completion_callback);
        completion_callback = NULL;
    }

    if (callback != NULL) {
        completion_callback = Block_copy(callback);
    }
    os_unfair_lock_unlock(&sink_state_lock);
}

void guest_execution_trace_sink_reset(void)
{
    os_unfair_lock_lock(&sink_state_lock);
    exit_event_received = false;
    last_exit_code = -1;
    completion_callback = NULL;
    interp_path_resolved = false;
    elf_exec_reached = false;
    main_elf_header_accepted = false;
    interp_header_loaded = false;
    interp_mappings_exist = false;
    main_image_loaded = false;
    auxv_initialized = false;
    resolved_interp_path[0] = '\0';
    last_loader_event[0] = '\0';
    interp_open_attempted = false;
    interp_open_succeeded = false;
    interp_open_errno = 0;
    do_execve_entered = false;
    format_exec_entered = false;
    before_elf_exec_entered = false;
    elf_exec_entered = false;
    begin_interval_calls_count = 0;
    end_interval_calls_count = 0;
    any_interval_received = false;
    ldrh_6d1c0_seen = false;
    ldrh_6d1c0_addr = 0;
    ldrh_6d1c0_val = 0;
    ldrh_6d1c0_mem_ret = 0;
    ldrh_6d1c0_host_ptr = 0;
    wb_6d1c0_seen = false;
    wb_6d1c0_x0_after = 0;
    wb_6d1c0_value = 0;
    wb_6d1c0_rt = 0;
    wb_6d1c0_size = 0;
    wb_6d1c0_is_64bit = 0;
    memset(x0_mutations, 0, sizeof(x0_mutations));
    memset(compile_pc_table, 0, sizeof(compile_pc_table));
    compile_entry_count = 0;
    compile_unique_pc_count = 0;
    compile_multigeneration_pc_count = 0;
    compile_max_count_per_pc = 0;
    compile_max_generation_count_per_pc = 0;
    compile_hottest_mmu = 0;
    compile_hottest_pc = 0;
    compile_hottest_generation_count = 0;
    cache_alloc_success_count = 0;
    cache_alloc_failure_count = 0;
    exec_entry_count = 0;
    exec_hottest_pc = 0;
    exec_hottest_count = 0;
    memset(exec_pc_table, 0, sizeof(exec_pc_table));
    root_stat_attempt_count = 0;
    root_stat_ok_count = 0;
    root_stat_fail_count = 0;
    stdout_header_write_count = 0;
    stdout_metadata_write_count = 0;
    stdout_prompt_write_count = 0;
    pty_header_write_count = 0;
    pty_metadata_write_count = 0;
    pty_prompt_write_count = 0;
    uname_syscall_entered = false;
    uname_syscall_returned = false;
    uname_syscall_return_value = 0;
    stdout_aarch64_write_observed = false;
    pty_aarch64_write_observed = false;
    os_unfair_lock_unlock(&sink_state_lock);
}

// Accessors omitted here are identical to header declarations and focus on proof queries
// Provide minimal set required by tests
bool guest_execution_trace_sink_interp_path_resolved(void) { return interp_path_resolved; }
bool guest_execution_trace_sink_elf_exec_reached(void) { return elf_exec_reached; }
bool guest_execution_trace_sink_main_elf_header_accepted(void) { return main_elf_header_accepted; }
bool guest_execution_trace_sink_interp_header_loaded(void) { return interp_header_loaded; }
bool guest_execution_trace_sink_interp_mappings_exist(void) { return interp_mappings_exist; }
bool guest_execution_trace_sink_main_image_loaded(void) { return main_image_loaded; }
bool guest_execution_trace_sink_auxv_initialized(void) { return auxv_initialized; }
const char *guest_execution_trace_sink_get_interp_path(void) { return resolved_interp_path; }
const char *guest_execution_trace_sink_get_last_loader_event(void) { return last_loader_event; }
bool guest_execution_trace_sink_interp_open_attempted(void) { return interp_open_attempted; }
bool guest_execution_trace_sink_interp_open_succeeded(void) { return interp_open_succeeded; }
int guest_execution_trace_sink_interp_open_errno(void) { return interp_open_errno; }
bool guest_execution_trace_sink_do_execve_entered(void) { return do_execve_entered; }
bool guest_execution_trace_sink_format_exec_entered(void) { return format_exec_entered; }
bool guest_execution_trace_sink_before_elf_exec_entered(void) { return before_elf_exec_entered; }
bool guest_execution_trace_sink_elf_exec_entered(void) { return elf_exec_entered; }
uint64_t guest_execution_trace_sink_begin_interval_calls_count(void) { return begin_interval_calls_count; }
uint64_t guest_execution_trace_sink_end_interval_calls_count(void) { return end_interval_calls_count; }
bool guest_execution_trace_sink_any_interval_received(void) { return any_interval_received; }
bool guest_execution_trace_sink_has_ldrh_6d1c0(void) { bool v; os_unfair_lock_lock(&sink_state_lock); v = ldrh_6d1c0_seen; os_unfair_lock_unlock(&sink_state_lock); return v; }
void guest_execution_trace_sink_get_ldrh_6d1c0(uint64_t *addr, uint16_t *read_val, int *mem_ret, uint64_t *host_ptr) { os_unfair_lock_lock(&sink_state_lock); if (addr) *addr = ldrh_6d1c0_addr; if (read_val) *read_val = ldrh_6d1c0_val; if (mem_ret) *mem_ret = ldrh_6d1c0_mem_ret; if (host_ptr) *host_ptr = ldrh_6d1c0_host_ptr; os_unfair_lock_unlock(&sink_state_lock); }
bool guest_execution_trace_sink_has_6d1c0_writeback(void) { bool v; os_unfair_lock_lock(&sink_state_lock); v = wb_6d1c0_seen; os_unfair_lock_unlock(&sink_state_lock); return v; }
void guest_execution_trace_sink_get_6d1c0_writeback(uint64_t *x0_after_write, uint64_t *value, unsigned long *rt, unsigned long *size, int *is_64bit) { os_unfair_lock_lock(&sink_state_lock); if (x0_after_write) *x0_after_write = wb_6d1c0_x0_after; if (value) *value = wb_6d1c0_value; if (rt) *rt = (unsigned long)wb_6d1c0_rt; if (size) *size = (unsigned long)wb_6d1c0_size; if (is_64bit) *is_64bit = wb_6d1c0_is_64bit; os_unfair_lock_unlock(&sink_state_lock); }
bool guest_execution_trace_sink_any_x0_mutation(void) { bool any=false; os_unfair_lock_lock(&sink_state_lock); for (int i=0;i<X0_MUTATION_TABLE_SIZE;i++){ if (x0_mutations[i].used) { any=true; break; } } os_unfair_lock_unlock(&sink_state_lock); return any; }
bool guest_execution_trace_sink_has_x0_mutation_at(uint64_t pc) { bool found=false; os_unfair_lock_lock(&sink_state_lock); for (int i=0;i<X0_MUTATION_TABLE_SIZE;i++){ if (x0_mutations[i].used && x0_mutations[i].pc == pc) { found=true; break; } } os_unfair_lock_unlock(&sink_state_lock); return found; }
bool guest_execution_trace_sink_get_x0_mutation_at(uint64_t pc, uint64_t *new_x0, uint64_t *old_x0, uint64_t *value, unsigned long *size, int *is_64bit) { bool found=false; os_unfair_lock_lock(&sink_state_lock); for (int i=0;i<X0_MUTATION_TABLE_SIZE;i++){ if (x0_mutations[i].used && x0_mutations[i].pc == pc) { if (new_x0) *new_x0 = x0_mutations[i].new_x0; if (old_x0) *old_x0 = x0_mutations[i].old_x0; if (value) *value = x0_mutations[i].value; if (size) *size = (unsigned long)x0_mutations[i].size; if (is_64bit) *is_64bit = x0_mutations[i].is_64bit; found=true; break; } } os_unfair_lock_unlock(&sink_state_lock); return found; }
uint64_t guest_execution_trace_sink_compile_entry_count(void) { uint64_t value; os_unfair_lock_lock(&sink_state_lock); value = compile_entry_count; os_unfair_lock_unlock(&sink_state_lock); return value; }
uint64_t guest_execution_trace_sink_compile_unique_pc_count(void) { uint64_t value; os_unfair_lock_lock(&sink_state_lock); value = compile_unique_pc_count; os_unfair_lock_unlock(&sink_state_lock); return value; }
uint64_t guest_execution_trace_sink_compile_multigeneration_pc_count(void) { uint64_t value; os_unfair_lock_lock(&sink_state_lock); value = compile_multigeneration_pc_count; os_unfair_lock_unlock(&sink_state_lock); return value; }
uint64_t guest_execution_trace_sink_compile_max_count_per_pc(void) { uint64_t value; os_unfair_lock_lock(&sink_state_lock); value = compile_max_count_per_pc; os_unfair_lock_unlock(&sink_state_lock); return value; }
uint64_t guest_execution_trace_sink_compile_max_generation_count_per_pc(void) { uint64_t value; os_unfair_lock_lock(&sink_state_lock); value = compile_max_generation_count_per_pc; os_unfair_lock_unlock(&sink_state_lock); return value; }
void guest_execution_trace_sink_get_compile_hottest(uint64_t *mmu, uint64_t *pc, uint64_t *count,
                                                    uint64_t *generation_count) {
    os_unfair_lock_lock(&sink_state_lock);
    if (mmu) *mmu = compile_hottest_mmu;
    if (pc) *pc = compile_hottest_pc;
    if (count) *count = compile_max_count_per_pc;
    if (generation_count) *generation_count = compile_hottest_generation_count;
    os_unfair_lock_unlock(&sink_state_lock);
}
void guest_execution_trace_sink_get_compile_hottest_cache_stats(uint64_t *l0_hits,
                                                                uint64_t *l1_hits,
                                                                uint64_t *inserts,
                                                                uint64_t *compile_failures) {
    os_unfair_lock_lock(&sink_state_lock);
    struct compile_pc_entry *entry =
        lookup_compile_entry(compile_hottest_mmu, compile_hottest_pc, false);
    if (l0_hits) *l0_hits = entry ? entry->l0_hit_count : 0;
    if (l1_hits) *l1_hits = entry ? entry->l1_hit_count : 0;
    if (inserts) *inserts = entry ? entry->insert_count : 0;
    if (compile_failures) *compile_failures = entry ? entry->compile_failure_count : 0;
    os_unfair_lock_unlock(&sink_state_lock);
}
bool guest_execution_trace_sink_get_compile_stats_for_pc(uint64_t mmu, uint64_t pc,
                                                         uint64_t *compile_count,
                                                         uint64_t *generation_count,
                                                         uint64_t *l0_hits,
                                                         uint64_t *l1_hits,
                                                         uint64_t *inserts,
                                                         uint64_t *compile_failures) {
    bool found = false;
    os_unfair_lock_lock(&sink_state_lock);
    struct compile_pc_entry *entry = lookup_compile_entry(mmu, pc, false);
    found = entry != NULL;
    if (compile_count) *compile_count = entry ? entry->compile_count : 0;
    if (generation_count) *generation_count = entry ? entry->generation_count : 0;
    if (l0_hits) *l0_hits = entry ? entry->l0_hit_count : 0;
    if (l1_hits) *l1_hits = entry ? entry->l1_hit_count : 0;
    if (inserts) *inserts = entry ? entry->insert_count : 0;
    if (compile_failures) *compile_failures = entry ? entry->compile_failure_count : 0;
    os_unfair_lock_unlock(&sink_state_lock);
    return found;
}
bool guest_execution_trace_sink_get_compile_miss_stats_for_pc(uint64_t mmu, uint64_t pc,
                                                              uint64_t *not_found_misses,
                                                              uint64_t *jetsam_misses,
                                                              uint64_t *generation_misses) {
    bool found = false;
    os_unfair_lock_lock(&sink_state_lock);
    struct compile_pc_entry *entry = lookup_compile_entry(mmu, pc, false);
    found = entry != NULL;
    if (not_found_misses) *not_found_misses = entry ? entry->miss_not_found_count : 0;
    if (jetsam_misses) *jetsam_misses = entry ? entry->miss_jetsam_count : 0;
    if (generation_misses) *generation_misses = entry ? entry->miss_generation_count : 0;
    os_unfair_lock_unlock(&sink_state_lock);
    return found;
}
void guest_execution_trace_sink_get_cache_alloc_stats(uint64_t *alloc_successes,
                                                      uint64_t *alloc_failures) {
    os_unfair_lock_lock(&sink_state_lock);
    if (alloc_successes) *alloc_successes = cache_alloc_success_count;
    if (alloc_failures) *alloc_failures = cache_alloc_failure_count;
    os_unfair_lock_unlock(&sink_state_lock);
}
uint64_t guest_execution_trace_sink_exec_entry_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = exec_entry_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}
void guest_execution_trace_sink_get_exec_hottest(uint64_t *pc, uint64_t *count) {
    os_unfair_lock_lock(&sink_state_lock);
    if (pc) *pc = exec_hottest_pc;
    if (count) *count = exec_hottest_count;
    os_unfair_lock_unlock(&sink_state_lock);
}
bool guest_execution_trace_sink_get_exec_count_for_pc(uint64_t pc, uint64_t *count) {
    bool found = false;
    os_unfair_lock_lock(&sink_state_lock);
    size_t slot = exec_pc_slot(pc);
    for (size_t probe = 0; probe < EXEC_PC_TABLE_SIZE; probe++) {
        struct exec_pc_entry *entry = &exec_pc_table[(slot + probe) & (EXEC_PC_TABLE_SIZE - 1)];
        if (!entry->used)
            break;
        if (entry->pc == pc) {
            if (count) *count = entry->exec_count;
            found = true;
            break;
        }
    }
    if (!found && count)
        *count = 0;
    os_unfair_lock_unlock(&sink_state_lock);
    return found;
}
uint64_t guest_execution_trace_sink_root_stat_attempt_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = root_stat_attempt_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}
uint64_t guest_execution_trace_sink_root_stat_ok_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = root_stat_ok_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}
uint64_t guest_execution_trace_sink_root_stat_fail_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = root_stat_fail_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

uint64_t guest_execution_trace_sink_stdout_header_write_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = stdout_header_write_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

uint64_t guest_execution_trace_sink_stdout_metadata_write_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = stdout_metadata_write_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

uint64_t guest_execution_trace_sink_stdout_prompt_write_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = stdout_prompt_write_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

uint64_t guest_execution_trace_sink_pty_header_write_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = pty_header_write_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

uint64_t guest_execution_trace_sink_pty_metadata_write_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = pty_metadata_write_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

uint64_t guest_execution_trace_sink_pty_prompt_write_count(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = pty_prompt_write_count;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

bool guest_execution_trace_sink_uname_syscall_entered(void) {
    bool value;
    os_unfair_lock_lock(&sink_state_lock);
    value = uname_syscall_entered;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

bool guest_execution_trace_sink_uname_syscall_returned(void) {
    bool value;
    os_unfair_lock_lock(&sink_state_lock);
    value = uname_syscall_returned;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

uint64_t guest_execution_trace_sink_uname_syscall_return_value(void) {
    uint64_t value;
    os_unfair_lock_lock(&sink_state_lock);
    value = uname_syscall_return_value;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

bool guest_execution_trace_sink_stdout_aarch64_write_observed(void) {
    bool value;
    os_unfair_lock_lock(&sink_state_lock);
    value = stdout_aarch64_write_observed;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

bool guest_execution_trace_sink_pty_aarch64_write_observed(void) {
    bool value;
    os_unfair_lock_lock(&sink_state_lock);
    value = pty_aarch64_write_observed;
    os_unfair_lock_unlock(&sink_state_lock);
    return value;
}

/* End of test-only trace sink */
