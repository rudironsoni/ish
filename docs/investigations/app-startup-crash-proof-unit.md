# Track A: Smallest Lawful Proof Unit - Task Visibility Bug

**Status:** PROOF UNIT DEFINED  
**Priority:** ACTIVE  
**Evidence Quality:** High (based on trace data)

---

## Executive Summary

The iOS app crash has been reduced to a **deterministic proof unit** around task/thread memory visibility. The evidence clearly shows that child threads cannot see task structure fields written by parent threads, despite memory barriers.

---

## Proof Unit Definition

### The Problem (Confirmed by Evidence)

From `crash_signature.json`:
```
[TRACE] seq=5 event=TASK_THREAD_CURRENT_SET pc=0x0 pid=0 mm=0x0 mem=0x0
[TRACE] seq=6 event=TASK_RUN_CURRENT_ENTRY_CHECK pc=0x0 current=0x1076bc000
[TRACE] seq=7 event=TASK_RUN_CURRENT_ENTRY pc=0x0 current=0x1076bc000 pid=0
[TRACE] seq=8 event=TASK_RUN_CURRENT_MEM_CHECK pc=0x0 mm=0x0 mem=0x0
```

**Key Finding:**
- `current` pointer is **valid** (0x1076bc000)
- `pid` field is **0** (should be 2 for login task)
- `mm` field is **0x0** (should be valid mm pointer)
- `mem` field is **0x0** (should be valid mem pointer)

**Conclusion:** The task struct is allocated and the pointer is passed correctly, but the **contents** are not visible to the child thread.

---

## Where Values Are Written vs Read

### Write Path (Parent Thread - Main iOS Thread)

```
TerminalViewController.m:startNewSession()
  └── become_new_init_child() [kernel/init.c:144]
      └── construct_task(init) [kernel/init.c:150]
          └── task_create_(parent) [kernel/task.c:task_create_]
              └── task->pid = pid->id [line ~54]
          └── task_set_mm(task, new_mm) [kernel/init.c:79]
              └── task->mm = mm [kernel/task.h:103]
              └── task->mem = &task->mm->mem [kernel/task.h:104]
      └── trace_emit_construct_task_done() [init.c:174-175]
          └── EMITS: pid, task_ptr, mm, mem (ALL VALID)
      └── __sync_synchronize() [init.c:164]
      └── current = task [init.c:166]
      └── __sync_synchronize() [init.c:171]
      └── return to TerminalViewController
  └── do_execve("/bin/login") [exec.c]
      └── mm_release(old_mm)
      └── task_set_mm(current, new_mm) [exec.c:373]
          └── Sets current->mm and current->mem
  └── __sync_synchronize() [TerminalViewController.m:205]
  └── task_start(current) [TerminalViewController.m:207]
```

### Read Path (Child Thread)

```
task_start(current) [kernel/task.c:192]
  └── __sync_synchronize() [line 193]
  └── pthread_create(&task->thread, ..., task_thread, task) [line 207]
      └── NEW THREAD: task_thread(task) [kernel/task.c:163]
          └── trace_emit_task_thread_entry((uint64_t)task) [line 166]
          └── trace_emit_task_thread_before_set(task, current) [line 169]
          └── current = task [line 171]
          └── trace_emit_task_thread_after_set(task, current) [line 174]
          └── __sync_synchronize() [line 179]
          └── trace_emit_task_thread_current_set(pid, mm, mem) [line 180]
              └── READS: current->pid, current->mm, current->mem
              └── OBSERVES: pid=0, mm=0x0, mem=0x0
```

---

## The Visibility Gap

### What's Being Synchronized

1. **Parent thread writes:**
   - task->pid = 2 (in task_create_)
   - task->mm = valid_ptr (in task_set_mm)
   - task->mem = valid_ptr (in task_set_mm)

2. **Memory barriers exist at:**
   - construct_task end: `__sync_synchronize()` [init.c]
   - become_new_init_child: Two barriers around `current = task` [init.c:164,171]
   - TerminalViewController: `__sync_synchronize()` [line 205]
   - task_start: `__sync_synchronize()` [task.c:193]
   - task_thread: `__sync_synchronize()` [task.c:179]

3. **Yet child thread sees:**
   - current->pid = 0
   - current->mm = 0x0
   - current->mem = 0x0

### The Puzzle

**Question:** Why don't the memory barriers ensure visibility?

**Possible explanations:**

1. **Wrong synchronization target:**
   - Barriers synchronize the parent thread's view
   - But the child thread starts AFTER the barriers complete
   - Child has no "happens-before" relationship with parent's writes

2. **Thread-local storage issue:**
   - `current` is `__thread` (TLS) variable
   - Parent sets its TLS current
   - Child sets its own TLS current
   - These are different variables - no synchronization between them

3. **Pointer vs contents:**
   - Child receives task pointer via pthread_create argument
   - The pointer itself is visible
   - But the contents at that address aren't flushed from parent's cache

4. **Missing release/acquire semantics:**
   - `__sync_synchronize()` is a full barrier (sequentially consistent)
   - But we may need explicit release in parent and acquire in child
   - pthread_create should provide this, but perhaps not for the task struct contents

---

## Hypothesis Validation Status

### Current Leading Hypothesis

**Name:** `task_struct_visibility_through_argument`

**Statement:** 
The task struct fields written by the parent thread are not visible to the child thread because:
1. The task pointer is passed via pthread_create argument (visible)
2. But the contents at that address are in parent's cache (not flushed)
3. Memory barriers in parent don't flush to global visibility
4. Child thread reads from its own cache or memory, not seeing parent's writes

**Evidence:**
- `current` pointer is valid (0x1076bc000) in child thread
- `current->pid`, `current->mm`, `current->mem` are all 0/NULL
- This happens AFTER `__sync_synchronize()` in both parent and child

**Status:** UNVERIFIED - needs targeted proof

---

## Smallest Lawful Proof

### Proof Strategy

Add targeted trace events that capture:
1. **Parent thread** writes to task struct (what values are written)
2. **Child thread** reads from task struct (what values are read)
3. **Comparison** showing the discrepancy

### Required New Trace Events

We need to add trace events that read DIRECTLY from the task pointer (not via current):

**In task_thread(), BEFORE setting current:**
```c
// Read directly from task argument to avoid TLS issues
trace_emit_task_thread_via_argument(
    ((struct task*)task)->pid,
    (uint64_t)((struct task*)task)->mm,
    (uint64_t)((struct task*)task)->mem
);
```

**In task_thread(), AFTER setting current:**
```c
// Read via current TLS variable
trace_emit_task_thread_via_current(
    current->pid,
    (uint64_t)current->mm,
    (uint64_t)current->mem
);
```

**Comparison:**
If `via_argument` shows valid values but `via_current` shows NULL, then:
- The task struct WAS initialized correctly
- But current TLS assignment failed or points to wrong memory

If both show NULL, then:
- The task struct was never properly initialized
- OR memory visibility is broken

---

## Proof Unit Implementation

### Step 1: Add New Trace Event Types

**File:** `trace/trace_events.def`

Add:
```c
/* Task visibility proof events */
TRACE_EVENT(TASK_THREAD_VIA_ARGUMENT, 1, TRACE_CAT_SYSCALL, 24)  /* pid, mm, mem from arg */
TRACE_EVENT(TASK_THREAD_VIA_CURRENT, 1, TRACE_CAT_SYSCALL, 24)   /* pid, mm, mem from current */
TRACE_EVENT(TASK_START_PRE_CREATE, 1, TRACE_CAT_SYSCALL, 32)     /* task_ptr, mm, mem, pid */
```

### Step 2: Add Trace Function Declarations

**File:** `trace/trace.h`

Add:
```c
void trace_emit_task_thread_via_argument(uint32_t pid, uint64_t mm, uint64_t mem);
void trace_emit_task_thread_via_current(uint32_t pid, uint64_t mm, uint64_t mem);
void trace_emit_task_start_pre_create(uint64_t task_ptr, uint64_t mm, uint64_t mem, uint32_t pid);
```

### Step 3: Implement Trace Functions

**File:** `trace/trace.c`

Add implementations following existing pattern.

### Step 4: Instrument Code

**File:** `kernel/task.c` - Modify `task_thread()`:
```c
static void *task_thread(void *task) {
    trace_emit_task_thread_entry((uint64_t)task);
    
    // PROOF: Read directly from task argument (before setting current)
    struct task *task_arg = (struct task*)task;
    trace_emit_task_thread_via_argument(
        task_arg->pid,
        (uint64_t)task_arg->mm,
        (uint64_t)task_arg->mem
    );
    
    trace_emit_task_thread_before_set((uint64_t)task, (uint64_t)current);
    
    current = task;
    
    trace_emit_task_thread_after_set((uint64_t)task, (uint64_t)current);
    
    __sync_synchronize();
    
    // PROOF: Read via current (after setting current)
    trace_emit_task_thread_via_current(
        current->pid,
        (uint64_t)current->mm,
        (uint64_t)current->mem
    );
    
    trace_emit_task_thread_current_set(current->pid, (uint64_t)current->mm, (uint64_t)current->mem);
    
    task_run_current();
    die("task_thread returned");
}
```

**File:** `kernel/task.c` - Modify `task_start()`:
```c
void task_start(struct task *task) {
    __sync_synchronize();
    
    // PROOF: Log task state before creating thread
    trace_emit_task_start_pre_create(
        (uint64_t)task,
        (uint64_t)task->mm,
        (uint64_t)task->mem,
        task->pid
    );
    
    trace_emit_task_start_pointer((uint64_t)task);
    trace_emit_task_start(task ? task->pid : 999999);
    
    if (pthread_create(&task->thread, &task_thread_attr, task_thread, task) < 0)
        die("could not create thread");
}
```

### Step 5: Run and Capture Trace

1. Build app with trace enabled
2. Run in simulator
3. Capture trace dump on crash
4. Analyze the proof events

---

## Expected Outcomes

### Scenario A: Task Arg Shows Valid, Current Shows NULL

**Interpretation:** The task struct was initialized correctly, but the `current` TLS assignment is broken.

**Likely causes:**
- TLS variable corruption
- Wrong thread-local storage implementation
- Compiler optimization issue with `__thread`

### Scenario B: Both Task Arg and Current Show NULL

**Interpretation:** The task struct was never properly initialized.

**Likely causes:**
- construct_task failed silently
- task_create_ didn't set fields
- Memory allocation returned zeroed memory

### Scenario C: Both Show Valid Values

**Interpretation:** Race condition - the bug only happens sometimes.

**Next step:** Add timing-sensitive instrumentation.

---

## Current Status

| Aspect | Status |
|--------|--------|
| Problem identified | YES - child thread sees NULL mm/mem |
| Root cause narrowed | YES - memory visibility or TLS issue |
| Proof unit defined | YES - compare task arg vs current reads |
| Trace events needed | YES - 3 new events |
| Implementation ready | YES - can proceed immediately |
| Verification pending | YES - need to run instrumented build |

---

## Recommended Immediate Action

1. **Implement the proof unit** (30 minutes)
   - Add trace event definitions
   - Add trace function declarations  
   - Add trace function implementations
   - Instrument task_thread() and task_start()

2. **Run the instrumented build** (15 minutes)
   - Build for simulator
   - Launch app
   - Let it crash
   - Capture trace dump

3. **Analyze results** (15 minutes)
   - Compare task arg reads vs current reads
   - Determine which hypothesis is correct
   - Proceed with targeted fix

**Total time to proof:** ~1 hour

---

## Conclusion

The crash has been reduced to a **smallest lawful proof unit**: verifying whether the task struct is properly initialized and whether the child thread can see those values through different access paths (direct pointer vs TLS current).

This proof unit is:
- **Minimal:** Only adds 3 trace events
- **Targeted:** Focuses specifically on the visibility question
- **Deterministic:** Will provide clear yes/no answer
- **Actionable:** Results directly inform the fix strategy
