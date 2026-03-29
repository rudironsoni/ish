# Track A: iOS App Startup Crash Boundary Report

**Status:** ACTIVE INVESTIGATION  
**Branch:** feat/aarch64-migration  
**Date:** 2026-03-29  
**Investigation Phase:** A3 - Defining Minimal Proof Unit

---

## Executive Summary

The iOS app startup crash occurs at the boundary between `bin_login_program_headers_read` and `guest_loop_entered`. The **current leading hypothesis** is a memory visibility issue in the task/thread initialization sequence, but this is **not yet proven**.

### Track A Status
- **Priority:** ACTIVE
- **Track B Status:** BLOCKED (broken Linux submodule)

---

## Phase A0: deps/linux Dependency Verdict (CONFIRMED)

```yaml
deps_linux_runtime_dependency_verdict:
  app_target: iSH (default)
  uses_kernel_mode: ish
  depends_on_deps_linux: no
  evidence:
    - "meson_options.txt: option('kernel', choices: ['ish', 'linux'], value: 'ish')"
    - "app/ProjectDebug.xcconfig: #include 'NotLinux.xcconfig'"
    - "app/NotLinux.xcconfig: NINJA_TARGETS = libish.a libish_emu.a libfakefs.a"
    - "Linux.xcconfig explicitly NOT included in default build"
  confidence: high
```

**Conclusion:** The crash is in the `kernel=ish` path, completely unrelated to `deps/linux`.

---

## Phase A1: Active Crash Path Mapping

### Milestone Evidence from boot_milestones.json

| Milestone | Status | Timestamp | Evidence |
|-----------|--------|-----------|----------|
| app_launched | completed | 11:14:59.814Z | "[Roots] File Provider not available" |
| boot_setup_started | completed | 11:14:59.819Z | "become_first_process: ENTRY" |
| first_elf_exec_entered | completed | 11:14:59.826Z | "/bin/busybox loaded" |
| first_elf_exec_returned | completed | 11:14:59.832Z | "do_execve returned with err=0" |
| second_execve_started | completed | 11:14:59.966Z | "/bin/login argc=3" |
| bin_login_elf_header_parsed | completed | 11:14:59.967Z | "read_header SUCCEEDED" |
| bin_login_program_headers_read | completed | 11:14:59.967Z | "read_prg_headers SUCCEEDED" |
| **guest_loop_entered** | **reached** | **11:14:59.970Z** | **"[TRACE] seq=2 event=TASK_THREAD_ENTRY"** |
| login_ready | failed | null | null |

**Highest Completed Milestone:** `bin_login_program_headers_read`  
**First Failing Milestone:** `guest_loop_entered`

### Crash Evidence from crash_signature.json

```
[TRACE] seq=5 event=TASK_THREAD_CURRENT_SET pc=0x0000000000000000 pid=0 mm=0x0 mem=0x0
task_run_current: ENTRY (current=0x1076bc000)
[TRACE] seq=7 event=TASK_RUN_CURRENT_ENTRY pc=0x0000000000000000 current=0x1076bc000 pid=0
[TRACE] seq=8 event=TASK_RUN_CURRENT_MEM_CHECK pc=0x0000000000000000 mm=0x0 mem=0x0
task_run_current: FATAL - current->mem is NULL!
```

**Key Observation:** The child thread sees:
- `current = 0x1076bc000` (valid pointer)
- `pid = 0` (should be 2 for login task)
- `mm = 0x0` (NULL - should be valid)
- `mem = 0x0` (NULL - should be valid)

---

## Phase A2: Hypothesis Table for current->mem / Task Startup

```yaml
hypothesis_table:
  - name: missing_publish_barrier_in_construct_task
    description: |
      construct_task() sets task->mm and task->mem via task_set_mm(),
      but the child thread doesn't see these writes due to missing
      memory barrier between construct_task() and task_start().
    status: plausible
    evidence_for:
      - construct_task() has __sync_synchronize() at end (line 97 in init.c)
      - task_thread() has __sync_synchronize() after setting current (line 179)
      - Yet child thread sees mm=0x0, mem=0x0 in trace at line 180
    evidence_against:
      - Parent thread (become_new_init_child) sets current = task before task_start()
      - Parent has memory barriers before/after setting current (lines 153, 160)
      - The issue may be which thread's current we're reading
    confidence: medium

  - name: wrong_thread_local_current_in_child
    description: |
      The __thread current variable is thread-local. Parent sets its own
      current, then task_thread() sets child's current. But child reads
      current->mm BEFORE current is set in child thread.
    status: proven_false
    evidence_for: []
    evidence_against:
      - task_thread() sets current = task at line 171 BEFORE reading current->mm
      - trace_emit_task_thread_current_set() at line 180 reads after current is set
    confidence: high

  - name: stale_task_struct_from_inheritance
    description: |
      construct_task() copies parent task via "*task = *parent" in task_create_.
      If parent had mm=NULL at copy time, child inherits stale values.
    status: disproven
    evidence_for: []
    evidence_against:
      - task_set_mm() is called AFTER construct_task returns in init.c line 79
      - trace_emit_construct_task_done at line 101 shows mm and mem are set
    confidence: high

  - name: task_set_mm_inline_not_visible
    description: |
      task_set_mm() is defined as static inline in task.h. The child thread
      may not see the writes from task_set_mm() due to compiler reordering
      or caching issues.
    status: weak_suspicion
    evidence_for:
      - task_set_mm is inline, which may cause visibility issues
      - Child sees mm=0x0 even though parent called task_set_mm
    evidence_against:
      - construct_task_done trace shows mm was set at line 101
      - task_set_mm is simple assignment, should be visible
    confidence: low

  - name: mm_new_returns_null_or_corrupted
    description: |
      mm_new() may fail or return corrupted memory, causing task_set_mm
      to set invalid values.
    status: disproven
    evidence_for: []
    evidence_against:
      - No error log from mm_new failure
      - exec.c mm_new succeeds (evidence from milestone completion)
    confidence: high

  - name: race_between_become_new_init_child_and_task_thread
    description: |
      become_new_init_child() sets current in parent thread, then
      TerminalViewController calls task_start(). But the parent thread's
      current is different from the child thread's current. Child sees
      the task pointer but not the mm/mem values written by construct_task.
    status: strongest_hypothesis
    evidence_for:
      - Parent thread sets its own current (line 155 in init.c)
      - Child thread sets its own current (line 171 in task.c)
      - These are DIFFERENT thread-local storage locations
      - Child's task pointer is passed via pthread_create argument
      - Child reads current->mm AFTER setting current, but values are 0
      - This suggests the task struct contents weren't flushed
    evidence_against:
      - Memory barriers exist at multiple points
    confidence: medium
```

---

## Phase A3: Current Status and Next Proof Unit

### Current Status

**Strongest Current Hypothesis:** `race_between_become_new_init_child_and_task_thread`

The evidence points to a visibility issue where:
1. **Parent thread** (main iOS thread) calls `construct_task()` which sets `task->mm` and `task->mem`
2. Parent sets its own thread-local `current = task`
3. Parent calls `task_start(task)` which creates a **new thread**
4. **Child thread** receives `task` pointer via pthread_create argument
5. Child sets its own thread-local `current = task`
6. Child reads `current->mm` and `current->mem` and sees **0x0**

**The Problem:** Even though both threads set `current` to point to the same `task` struct, the child thread doesn't see the `mm` and `mem` values written by the parent thread during `construct_task()`.

### Verification Status

**CONFIRMED FACTS:**
- `current` pointer is valid in child thread (0x1076bc000)
- `pid` field reads as 0 in child (should be 2)
- `mm` field reads as 0x0 in child (should be valid)
- `mem` field reads as 0x0 in child (should be valid)
- Memory barriers exist in construct_task(), become_new_init_child(), task_thread()

**INFERENCES:**
- The task struct is being shared between threads
- The writes from construct_task are not visible to the child thread
- This is likely a compiler or CPU reordering issue despite barriers

**UNVERIFIED SUSPICIONS:**
- Whether the inline task_set_mm causes visibility issues
- Whether the memory barriers are in the correct locations
- Whether there's a missing barrier between become_new_init_child and task_start

### Smallest Lawful Next Proof Unit

**Target:** Prove that the child thread sees stale task struct values.

**Method:** Add targeted instrumentation to trace the EXACT sequence:

1. **In construct_task() (init.c:79-101):**
   - Trace BEFORE task_set_mm
   - Trace AFTER task_set_mm
   - Trace memory addresses of task->mm and &task->mm->mem

2. **In become_new_init_child() (init.c:134-166):**
   - Trace BEFORE setting current
   - Trace AFTER setting current
   - Trace task->mm, task->mem values

3. **In TerminalViewController.m (line 205-207):**
   - Trace BEFORE __sync_synchronize()
   - Trace AFTER __sync_synchronize()
   - Trace current->mm, current->mem

4. **In task_start() (task.c:192-209):**
   - Trace BEFORE pthread_create
   - Trace task->mm, task->mem from parent thread

5. **In task_thread() (task.c:163-184):**
   - Trace IMMEDIATELY on entry (before setting current)
   - Trace task->mm, task->mem via the task argument pointer
   - Trace BEFORE setting current
   - Trace AFTER setting current
   - Trace BEFORE __sync_synchronize
   - Trace AFTER __sync_synchronize
   - Trace current->mm, current->mem

This will provide a **deterministic trace** showing:
- What values are written by parent
- What values are visible to child via argument pointer
- What values are visible to child via current after each step

### Alternative: Create a Minimal Harness Case

Create a focused test case in `tests/harness/task-visibility.c` that:
1. Creates a task struct
2. Sets mm and mem in main thread
3. Spawns a pthread
4. Child thread reads mm and mem
5. Asserts they match expected values

This isolates the problem from the full iOS app complexity.

---

## Required Answers to Investigation Questions

### 1. Where is current->mem assigned?

**Location:** `kernel/task.h:104` via `task_set_mm()` inline function

```c
static inline void task_set_mm(struct task *task, struct mm *mm) {
    trace_emit_task_set_mm((uint64_t)task, (uint64_t)mm);
    task->mm = mm;
    task->mem = &task->mm->mem;  // <-- HERE
    task->cpu.mmu = &task->mem->mmu;
}
```

**Called from:**
- `kernel/init.c:79` in `construct_task()`
- `kernel/exec.c:373` in `elf_exec()` (after mm_release)
- `kernel/fork.c:71` in copy_process

### 2. On which thread is it assigned?

**For the login task crash:**
- **Assigned by:** Main iOS thread (parent)
- **Location:** `kernel/init.c:79` within `construct_task()`
- **Call chain:** `become_new_init_child()` → `construct_task()` → `task_set_mm()`
- **Thread:** Main thread (the one executing TerminalViewController.m)

### 3. On which thread is it later observed as NULL?

**Observed by:** Child thread (new pthread)
- **Location:** `kernel/task.c:180` in `task_thread()`
- **Call chain:** `task_start()` → `pthread_create()` → `task_thread()` → `trace_emit_task_thread_current_set()`
- **Thread:** Newly created thread (task_thread)

**Observation evidence:**
```
[TRACE] seq=5 event=TASK_THREAD_CURRENT_SET pc=0x0000000000000000 pid=0 mm=0x0 mem=0x0
```

### 4. What synchronization exists between those points?

**Current synchronization chain:**

1. **In construct_task() (init.c:97-102):**
   ```c
   __sync_synchronize();  // Barrier before unlocking
   unlock(&pids_lock);
   trace_emit_construct_task_done(task->pid, (uint64_t)task, 
                                  (uint64_t)task->mm, (uint64_t)task->mem);
   ```

2. **In become_new_init_child() (init.c:153-160):**
   ```c
   __sync_synchronize();  // Before setting current
   current = task;
   __sync_synchronize();  // After setting current
   ```

3. **In TerminalViewController.m (line 205):**
   ```c
   __sync_synchronize();  // After do_execve, before task_start
   ```

4. **In task_start() (task.c:193):**
   ```c
   __sync_synchronize();  // Before pthread_create
   ```

5. **In task_thread() (task.c:179):**
   ```c
   __sync_synchronize();  // After setting current, before reading mm/mem
   ```

### 5. What is the specific failure mechanism?

**Current strongest theory:**

The parent thread writes `task->mm` and `task->mem` during `construct_task()`, but the child thread doesn't see these writes despite multiple memory barriers. This suggests:

**Either:**
- The barriers are not in the correct locations to ensure visibility
- The inline function `task_set_mm()` is being optimized in a way that prevents visibility
- There's a missing barrier between `become_new_init_child()` returning and `task_start()` being called
- The thread-local `current` variables are causing confusion about which thread's writes should be visible

**Or:**
- The task struct is being modified after construct_task() completes
- The mm/mem values are being cleared by some other code path

### 6. What is the smallest code path that reproduces it?

**Minimal reproduction:**
```c
// Main thread
struct task *task = construct_task(parent);  // Sets task->mm, task->mem
current = task;  // Set main thread's current
task_start(task);  // Creates pthread

// Child thread (task_thread)
static void *task_thread(void *task_arg) {
    struct task *task = task_arg;
    current = task;  // Set child thread's current
    __sync_synchronize();
    // CRASH: current->mem is NULL here
    task_run_current();
}
```

**The bug requires:**
1. Task created via construct_task() (sets mm/mem)
2. Current set in parent thread
3. task_start() called
4. Child thread sets current
5. Child reads current->mm/mem

---

## Track A Summary

| Question | Answer |
|----------|--------|
| Highest confirmed milestone | bin_login_program_headers_read |
| First failing milestone | guest_loop_entered |
| Strongest current hypothesis | race_between_become_new_init_child_and_task_thread |
| Hypothesis verification status | UNVERIFIED (needs targeted instrumentation) |
| Smallest lawful next proof unit | Add comprehensive trace points to verify memory visibility |
| New case or instrumentation | Instrumentation extension (trace points in task lifecycle) |

---

## Recommended Immediate Action

**Add targeted instrumentation** to prove/disprove the memory visibility hypothesis:

1. Modify `kernel/init.c` to add trace points in construct_task
2. Modify `kernel/task.c` to add trace points in task_start and task_thread
3. Modify `app/TerminalViewController.m` to add trace points around task_start
4. Re-run the app and examine the trace output
5. Compare parent thread writes vs child thread reads

This will provide deterministic evidence of the visibility issue location.
