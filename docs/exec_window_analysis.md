# Exec Window Analysis: mm_release/task_set_mm Vulnerability

## Feature ID
DIAG-002

## Overview

Analysis of the memory management lifecycle window in `kernel/exec.c` between `mm_release()` and `task_set_mm()` calls during ELF execution.

## Code Flow Analysis

### Vulnerable Window in exec.c

The vulnerability exists in `elf_exec()` function in `kernel/exec.c` (lines 265-295):

```c
// free the process's memory.
// from this point on, if any error occurs the process will have to be
// killed before it even starts.
lock(&current->general_lock);
printk("[exec] Calling mm_release, current->mm=%p\n", current->mm);
if (current->mm == NULL) {
    printk("[exec] ERROR: current->mm is NULL!\n");
    unlock(&current->general_lock);
    err = _EINVAL;
    goto out_free_interp;
}
mm_release(current->mm);
// TRACE WINDOW: after mm_release, before task_set_mm
// current->mem may point to freed memory here if refcount reached 0
trace_emit(TRACE_EVENT_MM_RELEASE, 0);  // Event to mark window start
printk("[exec] mm_release done, calling mm_new\n");
struct mm *new_mm = mm_new();
if (new_mm == NULL) {
    ISH_LOG_ERROR("ENOMEM: mm_new() failed");
    unlock(&current->general_lock);
    err = _ENOMEM;
    goto out_free_interp;
}
task_set_mm(current, new_mm);
// TRACE: task_set_mm completed, mm window closed
trace_emit(TRACE_EVENT_TASK_SET_MM, 0);  // Event to mark window end
```

### The Window

```
Timeline:
1. mm_release(current->mm)     -- OLD mm refcount decremented
2. [WINDOW START]              -- If refcount==0, OLD mm is FREED
                                 current->mm still points to OLD (stale)
                                 current->mem still points to &OLD->mem (stale)
3. mm_new()                    -- NEW mm allocated
4. task_set_mm(current, new_mm) -- current->mm = NEW
                                 current->mem = &NEW->mem
5. [WINDOW END]                -- Safe again
```

## Risk Assessment

### Severity: MEDIUM-HIGH

The window exists but is protected by `general_lock`. However, there are scenarios where this could still cause issues:

#### Scenario 1: Signal/Interrupt Handler
If a signal handler or interrupt handler runs during this window and accesses `current->mem`, it would access freed memory.

**Likelihood**: LOW (signals are blocked during critical sections)

#### Scenario 2: Procfs/External Access
If `/proc/PID/maps` or similar is read by another thread during this window, it could access the stale `current->mm` pointer.

**Likelihood**: MEDIUM (procfs reads hold pids_lock, not general_lock)

#### Scenario 3: Memory Allocator Behavior
If `mm_new()` returns the same memory address that was just freed (heap reuse), `current->mem` would point to potentially uninitialized or corrupted data.

**Likelihood**: MEDIUM (depends on allocator behavior)

#### Scenario 4: Error Path
If `mm_new()` fails (returns NULL), the code currently returns with `current->mm` and `current->mem` pointing to freed memory.

**Likelihood**: LOW (ENOMEM is rare, but the error path is unsafe)

## Code Paths After mm_release

Between `mm_release()` and `task_set_mm()`, the following code executes:

1. `trace_emit()` - Safe, doesn't access `current->mem`
2. `printk()` - Safe, doesn't access `current->mem`
3. `mm_new()` - Safe, creates new mm
   - `malloc()` - Safe
   - `mem_init()` - Safe
   - Returns new_mm - Safe

All these operations are safe, but if any error occurs during `mm_new()`:
- The code jumps to `out_free_interp` with `current->mm` pointing to freed memory
- This could cause issues in error handling

## Mitigations Already in Place

1. **Lock Protection**: `general_lock` is held throughout the window
2. **NULL Check**: `current->mm` is checked before `mm_release()`
3. **Defensive Verification**: After `task_set_mm()`, there are sanity checks:
   ```c
   if (current->mem == NULL) {
       printk("[exec] FATAL: task_set_mm did not set current->mem properly!\n");
       current->mem = &current->mm->mem;
   }
   if (current->mem != &current->mm->mem) {
       printk("[exec] WARNING: current->mem != &current->mm->mem, correcting\n");
       current->mem = &current->mm->mem;
   }
   ```
4. **Trace Points**: `TRACE_EVENT_MM_RELEASE` and `TRACE_EVENT_TASK_SET_MM` mark the window boundaries

## Potential Improvements

### Option 1: Set to NULL During Window (Recommended)

Set `current->mm` and `current->mem` to NULL immediately after `mm_release()`:

```c
mm_release(current->mm);
current->mm = NULL;   // Explicitly mark as invalid
current->mem = NULL;  // Explicitly mark as invalid
struct mm *new_mm = mm_new();
if (new_mm == NULL) {
    // Safe: current->mm is already NULL, no stale pointer
    unlock(&current->general_lock);
    err = _ENOMEM;
    goto out_free_interp;
}
task_set_mm(current, new_mm);
```

**Pros**: Clear invalid state, prevents use-after-free
**Cons**: Requires checking for NULL in any code that might run during window

### Option 2: Create New mm Before Releasing Old

```c
struct mm *new_mm = mm_new();
if (new_mm == NULL) {
    unlock(&current->general_lock);
    err = _ENOMEM;
    goto out_free_interp;
}
struct mm *old_mm = current->mm;
task_set_mm(current, new_mm);  // Set new mm first
mm_release(old_mm);            // Then release old
```

**Pros**: No window where current->mem is invalid
**Cons**: Briefly have two mm structures, requires careful ordering

### Option 3: Add Safety Assertions

Add `BUG_ON()` or `assert()` checks before any access to `current->mem` during the window:

```c
mm_release(current->mm);
BUG_ON(current->mem == &current->mm->mem);  // Should be false now
// ... window code ...
task_set_mm(current, new_mm);
```

**Pros**: Catches bugs early in development
**Cons**: Only helps during debugging

## Conclusion

The exec window is a real vulnerability but is mitigated by:
1. Proper locking (`general_lock` held throughout)
2. Short duration (only a few operations)
3. Defensive checks after `task_set_mm()`

The primary risk is in the error path when `mm_new()` fails, leaving stale pointers. Option 1 (setting to NULL during the window) is the recommended fix.

## Related Code

- `kernel/exec.c:elf_exec()` - Main vulnerable function
- `kernel/mmap.c:mm_release()` - Frees the mm structure
- `kernel/task.h:task_set_mm()` - Updates task->mm and task->mem
- `kernel/task.c:task_run_current()` - Validates current->mem before use

## Trace Events

Use these trace events to monitor the window:
- `TRACE_EVENT_MM_RELEASE` - Marks start of window
- `TRACE_EVENT_TASK_SET_MM` - Marks end of window
- `TRACE_EVENT_MM_RELEASE_FREED` - Indicates mm was actually freed
