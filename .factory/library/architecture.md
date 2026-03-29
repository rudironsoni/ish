# Architecture: iSH NULL current->mem Crash

## System Overview

iSH is an x86 emulator for iOS that runs a Linux-like environment. The crash occurs in the AArch64 emulator backend when the task system fails to properly initialize the memory management pointer.

## Components

### Task System (kernel/task.c, kernel/task.h)
- `struct task` - Process/task structure containing mm, mem, cpu state
- `task_create_(parent)` - Creates new task, copies from parent if not NULL
- `task_set_mm(task, mm)` - Atomically sets task->mm, task->mem, task->cpu.mmu
- `task_run_current()` - Validates current->mem before entering emulator
- `task_thread()` - Thread entry point with memory barrier

### Memory Management (kernel/mmap.c, kernel/mmap.h)
- `struct mm` - Memory management structure
- `struct mem` - Memory state with MMU
- `mm_new()` - Creates new mm with initialized mem
- `mm_copy(mm)` - Copies mm for fork
- `mm_retain/mm_release` - Reference counting

### Exec Path (kernel/exec.c)
- `elf_exec()` - Loads and executes ELF binary
- Contains mm_release/task_set_mm window

### Init Path (kernel/init.c)
- `become_first_process()` - Creates first task
- `construct_task()` - Helper for task creation
- Fully synchronous, no threading

### Fork Path (kernel/fork.c)
- `sys_clone()` - Creates new process/thread
- `copy_task()` - Copies task state
- Uses memory barriers for synchronization

## Data Flow

### Init Flow (Main Thread)
```
main()
  └── become_first_process()
        └── construct_task(NULL)
              ├── task_create_(NULL)   // Zero-initialized task
              ├── mm_new()             // Creates mm with mem
              ├── task_set_mm()        // Sets task->mm, task->mem
              └── return task
        └── current = task
task_run_current()  // current->mem guaranteed valid
```

### Fork Flow (Parent Thread -> Child Thread)
```
sys_clone()
  ├── task_create_(current)  // Copies current (with mm/mem)
  ├── copy_task(task, flags) // Sets new mm or retains parent's
  │     └── task_set_mm()    // Sets task->mm, task->mem
  └── task_start(task)       // Creates pthread
        └── task_thread(task)// Child thread starts
              ├── __sync_synchronize()
              ├── current = task
              └── task_run_current() // current->mem guaranteed valid
```

### Exec Flow (Current Thread)
```
elf_exec()
  ├── lock(&current->general_lock)
  ├── mm_release(current->mm)   // OLD mm freed
  ├── new_mm = mm_new()         // NEW mm allocated
  ├── task_set_mm(current, new_mm) // Sets current->mm, current->mem
  └── unlock(&current->general_lock)
```

## Crash Location

The crash occurs in `task_run_current()` at task.c:119:
```c
void task_run_current() {
    if (!current) {
        die("task_run_current: NULL current");
    }
    if (!current->mem) {  // LINE 119 - CRASH HERE
        die("task_run_current: NULL current->mem");
    }
    // ...
}
```

## Root Cause Hypotheses

### 1. Exec Window Bug (Most Likely)
In `exec.c`, `mm_release()` frees the old mm before `task_set_mm()` sets the new mm. If an error or signal occurs between these two calls, `current->mem` points to freed memory.

### 2. Use-After-Free
The mm is released while still referenced elsewhere, causing the mem pointer to become invalid.

### 3. Memory Corruption
Buffer overflow or other corruption zeroes out the task->mem field.

### 4. Current Initialization Bug
The thread-local `current` variable is not properly set in some edge case.

### Ruled Out: Race Condition
Code review confirmed proper synchronization. The init path is single-threaded. The fork path uses memory barriers correctly.

---

# Architecture: ISHInstrumentation Framework

## Overview

The ISHInstrumentation framework is an app-owned observability system that replaces the legacy trace subsystem. It provides semantic event recording and interval timing for the iSH app.

## Design Principles

1. **App-Owned**: The app layer owns bootstrap, activation, backend selection, recovery, persistence, and export
2. **Lower Layers Emit Only**: Kernel and emulator layers emit semantic events only
3. **Minimal Main**: main.m is minimal - just bootstrap + UIApplicationMain
4. **Task Zero Support**: Can disable Linux/emulator startup for app shell testing

## Components

### Objective-C Facade (ISHInstrumentation.h/m)
- **ISHInstrumentation**: Singleton class providing the public API
- **Event Enum**: BootstrapReady, LaunchBegan, LaunchReady, SceneConnected, SessionStarted, SessionReady, RecoveryDetected, ShutdownClean
- **Methods**: bootstrap, activate, isActive, recordEvent, beginInterval, endInterval

### C Bridge (ISHInstrumentationBridge.h/mm)
- **Origin Enum**: App, UI, Session, Kernel, Task, Exec, Emulator, TCTI
- **C Functions**: ish_instrumentation_* functions that bridge to Objective-C
- **Purpose**: Allows C code in kernel/emu to emit events

### Sinks
- **ISHInstrumentationSinkApple**: os_log and signpost integration
- **ISHInstrumentationOpenTelemetry**: OpenTelemetry bridge (stub initially)
- **ISHInstrumentationMetricKit**: MetricKit integration for diagnostics

### Semantic Events (ISHInstrumentationEvents.h)
- **App Events**: app.bootstrap.ready, app.launch.began, app.launch.ready, app.scene.connected
- **Session Events**: session.started, session.ready, session.bootstrap.deferred
- **Kernel Events**: task.created, task.started, exec.began, exec.mm.updated
- **Emulator Events**: emulator.started, tcti.dispatch.began
- **Boundary Events**: fatal.boundary, recovery.detected

### Compile-Time Flags (ISHRuntimeFlags.h)
- **ISH_TASK_ZERO_DISABLE_EMULATION**: When 1, disables Linux/emulator startup for app shell testing

## Data Flow

### Bootstrap Flow
```
main()
  └── [ISHInstrumentation bootstrap]
        └── ish_instrumentation_bootstrap()
              └── Create singleton, initialize sinks (NOP mode)
        └── return
  └── UIApplicationMain
        └── AppDelegate
              └── willFinishLaunchingWithOptions
                    └── [ISHInstrumentation activate]
                          └── ish_instrumentation_activate()
                                └── Switch sinks from NOP to active (os_log, MetricKit)
                    └── if (!ISH_TASK_ZERO_DISABLE_EMULATION)
                          └── Start Linux/emulator
                    └── else
                          └── Record session.bootstrap.deferred
```

### Event Emission Flow
```
Kernel Code
  └── ish_instrumentation_record_event(origin, name, attrs)
        └── ISHInstrumentationBridge.mm
              └── [ISHInstrumentation recordEvent:event]
                    └── Forwards to active sinks
                          ├── ISHInstrumentationSinkApple (os_log)
                          └── ISHInstrumentationOpenTelemetry (stub)
```

## Migration from Legacy Trace

### Old API (being removed)
- trace_init(), trace_shutdown()
- trace_emit_*(...) - event-specific functions
- trace_config_from_env()
- Startup markers, recovery logic, ring persistence

### New API (ISHInstrumentation)
- ish_instrumentation_bootstrap(), ish_instrumentation_activate()
- ish_instrumentation_record_event() - generic semantic event
- ish_instrumentation_begin_interval(), ish_instrumentation_end_interval()
- App-owned configuration, no env-driven policy

### Trace Shim (transition)
- trace.h reduced to minimal semantic API
- trace.c forwards to ish_instrumentation_* functions
- Eventually trace layer may be removed entirely
