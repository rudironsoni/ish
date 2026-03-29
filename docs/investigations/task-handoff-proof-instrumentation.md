# Task Handoff Proof Instrumentation

**Status:** IMPLEMENTED - Ready for testing  
**Date:** 2026-03-29  
**Purpose:** Determine the exact mechanism of the task/thread handoff failure

---

## Overview

This document describes the proof-quality instrumentation added to trace the task/thread handoff path. The instrumentation captures deterministic evidence of:
- Parent thread state before creating child
- Child thread state on entry
- Child thread state after setting `current`
- Child thread state before entering execution

---

## Trace Points Added

### 1. TASK_HANDOFF_PARENT_PRE_CREATE

**Location:** `kernel/task.c` - `task_start()` (line ~240)

**Purpose:** Capture parent thread state immediately before `pthread_create()`

**Logs:**
- host_thread_id: pthread_self() of parent
- task_ptr: pointer to task struct
- mm: task->mm value
- mem: task->mem value
- pid: task->pid value

**Code:**
```c
uint64_t host_thread_id = (uint64_t)pthread_self();
trace_emit_task_handoff_parent_pre_create(
    (uint64_t)task,
    task ? (uint64_t)task->mm : 0xDEAD,
    task ? (uint64_t)task->mem : 0xDEAD,
    task ? task->pid : 0xDEAD,
    host_thread_id
);
```

### 2. TASK_HANDOFF_CHILD_ENTRY

**Location:** `kernel/task.c` - `task_thread()` (line ~165)

**Purpose:** Capture child thread state immediately on entry, BEFORE setting `current`

**Logs:**
- host_thread_id: pthread_self() of child
- task_arg_ptr: pointer passed via pthread_create argument
- task_arg_mm: task->mm read directly from argument
- task_arg_mem: task->mem read directly from argument
- task_arg_pid: task->pid read directly from argument

**Code:**
```c
uint64_t host_thread_id = (uint64_t)pthread_self();
struct task *task_arg = (struct task*)task;
trace_emit_task_handoff_child_entry(
    (uint64_t)task_arg,
    task_arg ? (uint64_t)task_arg->mm : 0xDEAD,
    task_arg ? (uint64_t)task_arg->mem : 0xDEAD,
    task_arg ? task_arg->pid : 0xDEAD,
    host_thread_id
);
```

### 3. TASK_HANDOFF_CHILD_POST_CURRENT

**Location:** `kernel/task.c` - `task_thread()` (line ~190)

**Purpose:** Capture child thread state AFTER setting `current`, BEFORE `task_run_current()`

**Logs:**
- host_thread_id: pthread_self() of child
- current_ptr: value of current pointer
- current_mm: current->mm value
- current_mem: current->mem value
- current_pid: current->pid value

**Code:**
```c
__sync_synchronize();
trace_emit_task_handoff_child_post_current(
    (uint64_t)current,
    current ? (uint64_t)current->mm : 0xDEAD,
    current ? (uint64_t)current->mem : 0xDEAD,
    current ? current->pid : 0xDEAD,
    host_thread_id
);
```

### 4. TASK_HANDOFF_CHILD_PRE_RUN

**Location:** `kernel/task.c` - `task_run_current()` (line ~135)

**Purpose:** Capture child thread state immediately before guest execution

**Logs:**
- host_thread_id: pthread_self() of child
- current_ptr: value of current pointer
- current_mm: current->mm value
- current_mem: current->mem value
- current_pid: current->pid value

**Code:**
```c
uint64_t host_thread_id = (uint64_t)pthread_self();
trace_emit_task_handoff_child_pre_run(
    (uint64_t)current,
    current ? (uint64_t)current->mm : 0xDEAD,
    current ? (uint64_t)current->mem : 0xDEAD,
    current ? current->pid : 0xDEAD,
    host_thread_id
);
```

---

## Files Modified

1. **trace/trace_events.def**
   - Added 4 new TRACE_EVENT definitions

2. **trace/trace_types.h**
   - Added 4 new TRACE_EVENT_* enum values

3. **trace/trace.h**
   - Added 4 new trace_emit_* function declarations

4. **trace/trace.c**
   - Implemented 4 new trace_emit_* functions
   - Added helper function `trace_emit_task_handoff_record()`

5. **kernel/task.c**
   - Instrumented `task_start()` with PARENT_PRE_CREATE trace
   - Instrumented `task_thread()` with CHILD_ENTRY and CHILD_POST_CURRENT traces
   - Instrumented `task_run_current()` with CHILD_PRE_RUN trace

---

## Expected Trace Sequence

For a successful task handoff:

```
1. PARENT_PRE_CREATE
   task_ptr=0x1076bc000, mm=0x1076bd000, mem=0x1076bd008, pid=2
   host_thread_id=<parent_tid>

2. [pthread_create executed]

3. CHILD_ENTRY
   task_arg_ptr=0x1076bc000, task_arg_mm=0x1076bd000, task_arg_mem=0x1076bd008, task_arg_pid=2
   host_thread_id=<child_tid> (different from parent)

4. [current = task]

5. CHILD_POST_CURRENT
   current_ptr=0x1076bc000, current_mm=0x1076bd000, current_mem=0x1076bd008, current_pid=2
   host_thread_id=<child_tid>

6. [task_run_current() called]

7. CHILD_PRE_RUN
   current_ptr=0x1076bc000, current_mm=0x1076bd000, current_mem=0x1076bd008, current_pid=2
   host_thread_id=<child_tid>
```

---

## Questions to Answer from Trace

### Q1: Is the child receiving the same task pointer the parent created?

**Evidence:** Compare PARENT_PRE_CREATE.task_ptr vs CHILD_ENTRY.task_arg_ptr

- If **equal**: Child receives correct pointer
- If **different**: Wrong pointer passed to child

### Q2: Does the child see the parent's writes to task->mm and task->mem?

**Evidence:** Compare PARENT_PRE_CREATE values vs CHILD_ENTRY values

- If **all equal**: Memory visibility works, task was initialized correctly
- If **mm/mem/pid are NULL/0**: Task fields not visible to child

### Q3: Is `current` set to the expected task object?

**Evidence:** Compare CHILD_ENTRY.task_arg_ptr vs CHILD_POST_CURRENT.current_ptr

- If **equal**: current set correctly
- If **different**: current points to wrong object

### Q4: Is the issue task publication, `current` assignment, or later clobber?

**Evidence:** Compare trace point values:

- If PARENT values ≠ CHILD_ENTRY values: **Task publication issue**
- If CHILD_ENTRY values = valid, but CHILD_POST_CURRENT values = NULL: **Current assignment issue**
- If CHILD_POST_CURRENT values = valid, but CHILD_PRE_RUN values = NULL: **Later clobber/reset issue**

---

## Interpretation Scenarios

### Scenario A: Visibility Issue (Most Likely)

```
PARENT_PRE_CREATE:  task_ptr=0x1076bc000, mm=0x1076bd000, mem=0x1076bd008, pid=2
CHILD_ENTRY:        task_arg_ptr=0x1076bc000, task_arg_mm=0x0, task_arg_mem=0x0, task_arg_pid=0
CHILD_POST_CURRENT: current_ptr=0x1076bc000, current_mm=0x0, current_mem=0x0, current_pid=0
```

**Interpretation:**
- Child receives correct pointer (0x1076bc000)
- But task fields are all NULL/0
- This is a **memory visibility issue** - parent writes not visible to child

### Scenario B: Wrong Pointer

```
PARENT_PRE_CREATE:  task_ptr=0x1076bc000, mm=0x1076bd000, mem=0x1076bd008, pid=2
CHILD_ENTRY:        task_arg_ptr=0x1076be000, task_arg_mm=0x0, task_arg_mem=0x0, task_arg_pid=0
```

**Interpretation:**
- Child receives wrong pointer (0x1076be000 ≠ 0x1076bc000)
- This is a **pointer passing issue** - pthread_create receives wrong argument

### Scenario C: Current Assignment Issue

```
CHILD_ENTRY:        task_arg_ptr=0x1076bc000, task_arg_mm=0x1076bd000, task_arg_mem=0x1076bd008
CHILD_POST_CURRENT: current_ptr=0x1076bf000, current_mm=0x0, current_mem=0x0
```

**Interpretation:**
- Task arg has valid fields
- But current points to different object (0x1076bf000 ≠ 0x1076bc000)
- This is a **current assignment issue**

### Scenario D: Later Clobber

```
CHILD_POST_CURRENT: current_ptr=0x1076bc000, current_mm=0x1076bd000, current_mem=0x1076bd008
CHILD_PRE_RUN:      current_ptr=0x1076bc000, current_mm=0x0, current_mem=0x0
```

**Interpretation:**
- Values valid after setting current
- But NULL before task_run_current
- Something **clobbered the fields** between these points

---

## Next Steps

1. **Build the instrumented app:**
   ```bash
   meson compile -C builddir
   ```

2. **Run in iOS simulator** with trace enabled

3. **Capture trace dump** when crash occurs

4. **Analyze the 4 trace events** to determine which scenario matches

5. **Implement targeted fix** based on proven mechanism

---

## Build Status

✅ Compilation successful with no errors  
✅ All trace infrastructure in place  
✅ Ready for testing

---

## Summary

The proof instrumentation is now complete and provides deterministic evidence to answer:

1. ✅ Whether child receives expected task pointer
2. ✅ Whether task->mm and task->mem are visible in child
3. ✅ Whether they are visible before/after current assignment
4. ✅ Whether current and task are the same object
5. ✅ The exact failure mechanism (publication, assignment, or clobber)

The hypothesis will be **proven or disproven** by running the instrumented build and examining the trace output.
