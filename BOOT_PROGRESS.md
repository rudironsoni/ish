# ARM64 Guest Boot Progress

## Current State (2026-04-22)

### Commit: f3459146
**Fix**: Prepend "/" to filename in `setupRuntimeWithPath` for `do_execve` path resolution

### Checkpoint Status

#### ✅ PASSED: Harness Layer (H0-H4)
- H0: Harness entry
- H1: mount_root return (0 or -16)
- H2: become_first_process return (0 or -17)
- H3: do_execve call reached
- H4: do_execve return (0 = success)

#### ✅ PASSED: Exec/Loader Layer (X0-X3)
- X0: task.proof.do_execve.entry
- X1: task.proof.do_execve.before_format_exec
- X2: task.proof.do_execve.before_elf_exec
- X3: task.proof.elf_exec.after_return_to_caller

#### ✅ PASSED: ELF/Interpreter Layer (M1-D2.4)
- M1: Main ELF header accepted (class:2, machine:183, entry:0x7500)
- D2.0: Interpreter open succeeded (path: /lib/ld-musl-aarch64.so.1)
- D2.1: Interpreter header accepted (class:2, machine:183, entry:0x69604)
- D2.2: Interpreter program headers accepted (7 headers)
- D2.3: Interpreter PT_LOAD mapping (2 segments mapped)
- D2.4: Interpreter mappings exist (base:0x1000, entry:0x6a604)

#### ✅ PASSED: Early Execution Layer
- Interpreter entry PC established: 0x6a604
- First guest fetch completed
- First guest instruction executed
- Multiple blocks compiled and executed (0x6a604 → 0x6a620 → 0x6a62c → 0x6a634 → 0x6a640...)
- Load/store operations working (ldr with helper path)
- MMU/TLB translations functional (guest_ea → host_ptr mappings)
- Normal block exit flow observed

#### ❌ MISSING: First PTY Byte
Guest has not yet produced output to PTY.

### Test Results
- `testDynamicELF_B1_InterpreterPath_IsResolvedThroughRealExec`: **PASSED**

### Active Seam
**Between**: Early guest execution (interpreter running)
**And**: First PTY byte output

### Next Investigation Areas
1. Why is the guest not producing PTY output?
   - Is the guest reaching the write syscall?
   - Is the PTY properly initialized?
   - Is the guest stuck in a loop or waiting?
   - Is there a syscall handling issue?

2. Check guest execution limits:
   - The harness runs with `a64_cpu_run_limited(cpu, &exec_tlb, 10000)` - 10k instruction limit
   - Is the guest hitting this limit before reaching output?

3. Verify syscall path:
   - Does the test binary attempt to write to stdout/stderr?
   - Is the syscall handler reached?
   - Is the PTY fd properly set up?

### Files Modified
- `Tests/Support/GuestExecutionHarness/GuestExecutionHarness.m:250` - Fixed path for do_execve

### Proof Path
```bash
xcodebuild -project IXLand.xcodeproj \
  -scheme IXLandGuestRuntime \
  -destination 'platform=iOS Simulator,name=iPhone 17' \
  -derivedDataPath ./DerivedData \
  test-without-building \
  -only-testing:IXLandLinuxRuntimeSystemTests/GuestDynamicELFExecutionTests/testDynamicELF_B1_InterpreterPath_IsResolvedThroughRealExec
```
