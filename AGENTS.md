# iSH Development Guidelines for Agentic Coding Agents

This file provides essential information for AI agents working on the iSH codebase, including build/test commands and code style guidelines.

## 🔨 Build, Lint, and Test Commands

### Platform Detection
The build system automatically detects the platform (macOS vs Linux) and provides appropriate targets.

### macOS Targets (Xcode)
```bash
make build-sim      # Build for iOS Simulator (Debug)
make build-ios      # Build for iOS Device (Release)
make build-mac      # Build for macOS
make test-unit      # Run unit tests (XCTest) in simulator
make test-ui        # Run UI tests (XCUITest) in simulator
make test           # Run all tests (currently just unit tests)
make analyze        - Static analysis with clang
make archive        # Create release archive for distribution
```

### Build Commands (Meson + Ninja)
The project uses meson for building. After configuring, use ninja for building.
```bash
meson setup build              # Configure build directory
ninja -C build                # Build the project
ninja -C build test           # Run tests (if configured)
```

### Testing (aarch64)
Tests are located in `tests/aarch64/` and can be run with GCC directly or via the test runner:
```bash
# Run all aarch64 tests (recommended - 27 tests)
./tests/aarch64/run_all_tests.sh

# Build and run decoder tests specifically (40 tests)
gcc -I. tests/aarch64/decoder_test.c emu/aarch64/decode.c -o /tmp/decoder_test && /tmp/decoder_test

# Run generator tests
gcc -I. tests/aarch64/gen_test_simple.c -o /tmp/gen_test && /tmp/gen_test

# Run integration tests (10 tests)
gcc -I. tests/aarch64/integration_test.c tests/aarch64/gen_test_minimal.c \
    emu/aarch64/decode.c -o /tmp/integration_test && /tmp/integration_test
```

### Linting and Formatting
```bash
# Format code
find . -name '*.c' -o -name '*.h' | xargs clang-format -i

# Syntax check (gcc)
gcc -fsyntax-only -I. <file.c>

# Static analysis (requires clang-tidy)
clang-tidy -I. <source_file.c> --warnings-as-errors=*
```

### Docker Targets (Linux Environment)
```bash
make docker-build   # Build Docker images for testing
make docker-test    # Run tests in Docker container
make docker-e2e     # Run end-to-end tests in Docker
make docker-clean   # Clean Docker volumes and containers
```

### Cross-compilation Targets
```bash
make osxcross       # Build for macOS using osxcross (requires installation)
make docker-osxcross # Build for macOS using Docker (recommended)
```

## 📝 Code Style Guidelines

### File Organization
- Header files (.h) contain declarations, extern variables, and inline functions
- Source files (.c) contain function definitions and static variables
- Group related functions together with comment headers
- Keep files focused on a single responsibility

### Imports and Includes
```c
// Standard library includes first (angle brackets)
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Project includes next (quotes)
#include "kernel/calls.h"
#include "kernel/task.h"
#include "xX_main_Xx.h"
```

### Formatting
- Indentation: 4 spaces (configured in .editorconfig)
- No tab characters
- Line length: Aim for < 100 characters when possible
- Braces: Opening brace on new line for functions, same line for control structures
```c
int function_name(int param) {
    if (condition) {
        // statement
    } else {
        // statement
    }
    
    for (init; condition; increment) {
        // statement
    }
}
```

### Types and Variables
- Use fixed-width integer types from stdint.h when precision matters (uint32_t, int64_t)
- Pointer declarations: Type* variable (not Type *variable)
- Initialize variables when possible
- Use meaningful, descriptive names
- snake_case for variables and functions
```c
uint32_t instruction;
int64_t immediate_value;
char* buffer_pointer;
```

### Naming Conventions
- Functions: snake_case (e.g., `a64_decode`, `sign_extend`)
- Variables: snake_case (e.g., `opcode`, `register_value`)
- Macros: UPPER_SNAKE_CASE (e.g., `#define MAX_BUFFER_SIZE 256`)
- Enums: UPPER_SNAKE_CASE for values, PascalCase for type names
- Structs: PascalCase for type names, snake_case for members
```c
typedef enum {
    A64_EQ = 0,
    A64_NE = 1,
    // ...
} a64_cond_t;

typedef struct {
    uint32_t raw;
    uint16_t imm;
    // ...
} a64_instr_t;
```

### Error Handling
- Functions typically return 0 on success, negative error codes on failure
- Check return values from all function calls
- Propagate errors upward when appropriate
- Use standard error codes from errno.h when applicable
```c
int result = some_function(args);
if (result < 0) {
    fprintf(stderr, "Error: %s\n", strerror(-result));
    return result;
}
```

### Comments
- Use block comments (/ * * /) for file headers and section descriptions
- Use inline comments (//) for explaining non-obvious code
- Comment why, not what (unless the what is complex)
- Keep comments up-to-date when modifying code
```c
/* 
 * Main decode entry point
 * Decodes an A64 instruction into its components
 */
int a64_decode(uint32_t insn, a64_instr_t *out) {
    // Clear output structure
    memset(out, 0, sizeof(*out));
    out->raw = insn;
    
    // Check for system instructions first (SVC, HVC, hints, barriers)
    // These have top 8 bits = 0xD4 or 0xD5 (exception and system)
    uint8_t top_byte = (insn >> 24) & 0xFF;
    if (top_byte == 0xD4 || top_byte == 0xD5) {
        return a64_decode_system(insn, out);
    }
    // ... rest of function
}
```

### Safety and Security
- Always validate input parameters
- Use bounded string functions (strncpy, snprintf) instead of unsafe variants
- Check buffer bounds before memory operations
- Prefer stack allocation over heap when size is known and small
- Free allocated memory in error paths

### Assembly Code Guidelines
- Found primarily in interpreter code
- Use meaningful macro names when possible
- Group related instructions with comments
- Document non-obvious register usage
- Follow existing patterns in the codebase

## 🔧 TCTI (Threaded Code) Generator

### Important: Source of Truth
The TCTI assembly gadgets in `tcti/aarch64/gadgets_tcti_impl.c` and `tcti/aarch64/gadgets_tcti.h` are **AUTO-GENERATED** by `tcti/aarch64/tcti-gadget-gen.py`. 

**NEVER edit the .c or .h files directly** - your changes will be lost when the generator is run.

### Making Changes to Gadgets
1. Edit `tcti/aarch64/tcti-gadget-gen.py` to modify gadget generation logic
2. Regenerate the files:
   ```bash
   python3 tcti/aarch64/tcti-gadget-gen.py -o tcti/aarch64
   ```
3. Verify the generated code compiles

### Register Contract
The gadget generator establishes these register conventions:
- **Guest x0-x15** → **Host x1-x16** (direct TCTI-mapped, always hot)
- **Guest x16-x30** → Memory-backed in cpu_state (load/store via gadgets)
- **Guest SP (x31)** → Memory-backed in cpu_state (load/store via gadgets)
- **Host x14, x15** → Scratch temps for memory-backed operations
- **Host x28** → Bytecode pointer (gadget stream)
- **Host x29** → CPU state pointer

### Memory-Backed Operations
When operating on registers x16-x30 or SP:
1. Load into temp (x14/x15) using `gadget_load_xreg_16_to_30[]` or `gadget_load_sp`
2. Execute operation using the temp
3. Store back using `gadget_store_xreg_16_to_30[]` or `gadget_store_sp`

The generator (`tcti/aarch64/gen.c`) must respect these contracts when emitting bytecode.

## 🛠️ Development Workflow

### Making Changes
1. Create a descriptive branch name (feature/description or fix/issue-number)
2. Make small, focused commits
3. Write clear commit messages explaining why the change was made
4. Update relevant documentation when changing interfaces
5. Ensure tests pass before submitting pull requests

### Testing Practices
- Run appropriate tests for your changes
- For emulator changes: run decoder tests via `gcc -I. tests/aarch64/decoder_test.c emu/aarch64/decode.c -o /tmp/decoder_test && /tmp/decoder_test`
- For system call changes: consider adding test cases
- For UI changes: run UI tests on macOS
- For core functionality: run the full test suite via `./tests/aarch64/run_all_tests.sh`

### Debugging
- Use logging channels enabled via ISH_LOG environment variable or xcconfig
- Key log channels: strace (system calls), instr (instructions), verbose (debug)
- On Linux: Use gdb with tools/ptraceomatic for single-stepping
- On macOS: Use Xcode debugger or lldb

## 🚫 CRITICAL PROHIBITIONS - NO FALLBACK CODE

### NO FALLBACK OF ANY KIND, IMMEDIATE REMOVAL

Any interpreter fallback, step fallback, decode-and-step path, or C execution fallback is **strictly forbidden**.

**Forbidden Code Examples:**
- `emu/aarch64/cpu.c::a64_cpu_step()` - Single-instruction interpreter fallback
- Any function that decodes-and-executes instructions in C as a fallback path
- Any code path that bypasses TCTI for "unimplemented" instructions

**Agent Requirements:**
1. **MUST** remove forbidden fallback code immediately when found
2. **MUST NOT** preserve fallback code for debugging, bring-up, or coverage gaps
3. **MUST NOT** isolate fallback behind flags or compile-time switches
4. **MUST NOT** move fallback code elsewhere or comment it out
5. **MUST** treat missing TCTI coverage as a TCTI bug to be fixed in TCTI
6. **MUST NOT** introduce any replacement interpreter path
7. **MUST** report incomplete coverage plainly without offering fallback solutions

**This is not optional. This is not deferred. This is not a design discussion.**

If TCTI coverage is incomplete, that is a TCTI bug that **MUST** be reported and fixed in TCTI. It **MUST NOT** be bypassed by fallback code.

## 📚 Resources
- README.md: General project information and build instructions
- Wiki: https://github.com/ish-app/ish/wiki (detailed documentation)
- Issues: https://github.com/ish-app/ish/issues (bug tracking and feature requests)
- TEST_RESULTS.md: Recent test outcomes

This guide should help you navigate and contribute effectively to the iSH codebase. When in doubt, follow the existing patterns in the code you're modifying.

---

## Strict Operational Debugging Policy

### 1. Scope and Intent

This repository targets correct Linux AArch64 userspace and runtime behavior.

- Alpine is ONLY a validation target, NOT the product definition.
- Future distros such as Ubuntu MUST remain viable.
- Fixes MUST prefer distro-agnostic correctness over distro-specific hacks.
- No fix may be justified solely because "it works on Alpine" without proving generic correctness.

### 2. Layered Debugging Model

ALL debugging MUST proceed in this EXACT order:

1. **ISA truth** - Verify instruction semantics from the architecture specification.
2. **Decoder truth** - Verify decoded fields from `emu/aarch64/decode.c`.
3. **Generator truth** - Verify emitted gadgets and metadata from `tcti/aarch64/gen.c` or generator sources.
4. **Execution truth** - Verify actual register, flag, memory, and next-PC behavior.
5. **Runtime smoke test** - Verify end-to-end behavior ONLY after lower layers pass.

**Definitions:**
- ISA truth = architecture semantics from official specification.
- Decoder truth = decoded instruction fields produced by decoder.
- Generator truth = bytecode sequences and gadget pointers emitted by generator.
- Execution truth = observed behavior during actual TCTI execution.
- Runtime smoke test = confirmation that full runtime paths function.

**CRITICAL RULE:**
- Runtime symptoms are NOT sufficient proof of root cause.
- NO patch may be justified from runtime behavior alone if a smaller proof is possible.
- Execution truth MUST be proven before runtime smoke test is attempted.

### 3. Smallest Failing Unit Rule

EVERY bug MUST be reduced to EXACTLY ONE smallest failing unit before patching.

**Allowed units:**
- Single raw instruction.
- Two-instruction sequence.
- Tiny basic block.
- Process-entry fixture.
- Syscall fixture.
- Narrow runtime boundary proof.

**FORBIDDEN as first diagnostic target:**
- Full BusyBox boot.
- Full musl startup.
- Full distro boot.
- Broad runtime trace fishing.
- Large-scale runtime experiments.

**RULE:** When lower-level units are already proven, do NOT reopen them without new contradictory evidence.

### 4. Proven-Layers Discipline

Once a layer is proven for a unit, the agent MUST NOT reopen that layer unless:
- New evidence directly contradicts the previous proof.
- The previous proof is shown to be invalid.
- The current failing unit is demonstrably different.

**REQUIREMENT:** The agent MUST explicitly state:
- Which layers are already proven.
- Which single layer is currently under investigation.

### 5. Generated Code Policy

**GENERATED OUTPUT IS NOT A SOURCE OF TRUTH.**

- Generated gadget output MUST NEVER be edited directly.
- If a generated artifact appears wrong, fix the generator source, NOT the generated file.
- Acceptable sources of truth include:
  - `tcti/aarch64/tcti-gadget-gen.py`
  - Generation wiring such as Meson rules.
  - Handwritten source files.

**INVALID:** Any direct edit to generated output.

**REQUIREMENT:** The agent MUST state whether a change affects:
- Handwritten source.
- Generator source.
- Generated artifacts.

**ZERO-TOLERANCE:** Direct edits to generated files are forbidden and must be reverted.

### 6. Runtime Debugging Restrictions

Runtime debugging is allowed ONLY after the lower relevant layers have been proven for the current unit.

**FORBIDDEN:**
- Using broad runtime traces as a primary reasoning tool.
- Inferring root cause from high CPU, timeout, or crash frequency alone.
- Generalizing from one failing command to the whole emulator.
- Jumping from one runtime symptom to a broad subsystem patch.
- Runtime-first debugging when a smaller unit is provable.

### 7. Instrumentation Policy

Instrumentation is allowed ONLY when:
- The missing evidence cannot be obtained from existing tests or a debugger.
- The instrumentation is narrowly scoped to the current failing unit.

**REQUIREMENTS for instrumentation:**
- One-shot or narrow-range ONLY.
- Minimal.
- Easy to remove.
- Tied to a specific PC range, fixture, block boundary, or exact failing unit.

**FORBIDDEN:**
- Hot-path printk spam.
- Broad tracing.
- File-I/O tracing in hot paths.
- Indefinite diagnostic code left in tree.

**MANDATORY:** Temporary instrumentation MUST be removed immediately after the needed evidence is captured.

### 8. Patch Selection Rule

The first patch target MUST be:
- Exactly one file.
- Exactly one function.
- Chosen ONLY after the first actually failing layer is proven.

**FORBIDDEN:**
- Multi-subsystem speculative patches.
- Broad cleanup mixed with bug fixing.
- Patching several candidate causes at once.
- Changing production code before the broken layer is proven.
- Patching based on speculation.

### 9. Regression Rule

Every confirmed bug fix MUST leave behind ONE of:
- Decode golden test.
- Generator golden test.
- Semantic microtest.
- ABI fixture test.
- Narrow runtime regression.

**REQUIREMENT:** The regression MUST target the exact failing unit that justified the patch.

### 10. Cross-Distro Rule

Every fix MUST be labeled as one of:
- **Distro-agnostic core correctness** - applies to all AArch64 Linux.
- **Loader/libc-sensitive behavior** - specific to musl/glibc implementation details.
- **Temporary compatibility workaround** - explicitly labeled and minimized.

**REQUIREMENT:** Prefer distro-agnostic core correctness first. Isolate loader/libc-sensitive logic from core paths. Minimize and label temporary workarounds.

### 11. Required Per-Debug-Report Format

Every debug report MUST follow this template in this exact order:

#### Current Proven Layers
- List which layers are already proven for the current unit.

#### Current Failing Unit
- Define the single smallest failing unit.

#### Expected Truth
- ISA or ABI expectation for that unit.

#### Observed Truth
- Actual observed behavior at the currently investigated layer.

#### First Failing Layer
- One of: ISA, decoder, generator, execution, runtime boundary, ABI fixture, syscall fixture.

#### Exact Next Step
- One step only.
- No multiple options unless explicitly requested.

#### Exact Patch Target
- One file.
- One function.
- Only if the broken layer is proven.

#### Cleanup State
- Whether temporary instrumentation has been removed.

### 12. Forbidden Behaviors

**STRICTLY FORBIDDEN:**
- Asking broad "what should I do" questions.
- Asking for confirmation if the next smallest-step proof is obvious.
- Drifting back to full-runtime-first debugging.
- Reopening proven units casually.
- Patching based on speculation.
- Mixing unrelated fixes.
- Leaving diagnostic scaffolding behind.
- Editing generated outputs directly.
- Claiming a root cause before the first failing layer is proven.
- Jumping to broad loader or ABI theories before boundary proof.
- Using BusyBox-level speculation as primary reasoning.
- Reopening Units A, B, or C without new contradictory evidence.
---

## Tracing and Diagnostics Subsystem Policy

### PRIMARY RULE

For all debugging in this repository:

**FORBIDDEN:**
- Do NOT add one-off `fprintf`
- Do NOT add one-off `printk`
- Do NOT add hardcoded PC probes
- Do NOT add sticky diagnostics to `emu/aarch64/cpu.c`
- Do NOT add random debug globals
- Do NOT broaden runtime tracing by editing source

**REQUIRED:**
- Use the centralized trace subsystem
- Use runtime trace filters
- Use block sidecars
- Use the harness
- Use smallest-unit-first fixtures

If more visibility is needed, first increase trace level or narrow filters.
Only if the tracing subsystem itself is insufficient for the current bug may you extend it, and even then you must do so generically, not as a one-off hack.

### MANDATORY DEBUGGING ORDER

Work in this exact order:

1. Define the smallest failing unit
2. State expected truth
3. Choose the right harness mode
4. Run tracing at the lowest useful level
5. Inspect artifacts
6. Identify the first failing layer
7. Patch one file and one function only
8. Add or update regression
9. Rerun the harness and verify

Do NOT start from full runtime smoke tests unless the bug cannot be reduced further.

### CHOOSE EXACTLY ONE HARNESS MODE FIRST

For every bug, choose exactly one starting mode:

**A. Decode golden**
- Use when the question is: does raw instruction decode correctly?

**B. Generator golden**
- Use when the question is: does decoded instruction emit the correct gadget sequence?

**C. Semantic microtest**
- Use when the question is: does one instruction or tiny sequence execute correctly?

**D. ABI fixture**
- Use when the question is: is process entry, stack, auxv, TLS, or loader handoff correct?

**E. Runtime trace fixture**
- Use when the bug only appears in actual runtime flow and lower layers are already proven.

Do NOT jump to mode E if A through D can answer the question.

### TRACE LEVEL RULES

Start with the lowest useful trace level:

**Level 1 (summary):**
- Use for: compile summaries, top-level fault summaries, high-level process start, unsupported conditions

**Level 2 (boundary):**
- Use for: block entry/exit, syscall entry/exit, fault boundaries, smallest runtime boundary proofs

**Level 3 (block):**
- Use for: decoded block-level understanding, block sidecar inspection, selected register snapshots, instruction list and block mapping

**Level 4 (instruction):**
- Use ONLY for narrow PC ranges and only when levels 2 and 3 are insufficient.

**Level 5 (forensic):**
- Use ONLY for very narrow captures and only as a last resort.

**Default escalation order:**
- Start at level 2 for runtime issues
- Then level 3
- Then level 4 with tight filters
- Never jump to 4 or 5 broadly

### FILTER RULES

Narrow tracing BEFORE increasing verbosity.

Always try to use:
- `ISH_TRACE_PC`
- `ISH_TRACE_EVENTS`
- `ISH_TRACE_REGS`
- `ISH_TRACE_DUMP_ON`
- `ISH_TRACE_RING_SIZE`
- `ISH_TRACE_OUT`

**Examples of required behavior:**

If debugging one block range:
```bash
ISH_TRACE_PC=<start>-<end>
```

If debugging block boundaries only:
```bash
ISH_TRACE_EVENTS=block.boundary,fault,syscall
```

If debugging selected registers:
```bash
ISH_TRACE_REGS=x2,x3,x4
```

If debugging runtime failures:
```bash
ISH_TRACE_DUMP_ON=fault
```

Do NOT enable broad high-volume tracing over the full runtime unless explicitly necessary.

### REQUIRED DEFAULT COMMAND PATTERNS

When debugging a runtime bug, use these command patterns first.

**Boundary-first run:**
```bash
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=2 ISH_TRACE_DUMP_ON=fault ./build/ish ...
```

**Boundary-first with PC filter:**
```bash
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=2 ISH_TRACE_PC=<start>-<end> ISH_TRACE_DUMP_ON=fault ./build/ish ...
```

**Block-level run:**
```bash
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=3 ISH_TRACE_PC=<start>-<end> ISH_TRACE_EVENTS=block.compile,block.entry,block.exit,fault ./build/ish ...
```

**Selected-register run:**
```bash
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=3 ISH_TRACE_PC=<start>-<end> ISH_TRACE_REGS=x2,x3,x4 ./build/ish ...
```

**Offline decode:**
```bash
python3 scripts/trace_decode.py <trace-file>
python3 scripts/trace_decode.py <trace-file> -f json -o <decoded.json>
```

Prefer these patterns over source edits.

### WHEN TO USE BLOCK SIDECARS

If the bug involves:
- Wrong block composition
- Wrong guest-instruction grouping
- Wrong emitted block coverage
- Wrong guest-PC to gadget mapping
- Confusion about the current block shape

Then inspect the block sidecar before proposing a patch.

Use the sidecar to answer:
- Block start and end PC
- Instruction list
- Raw instructions
- Decoded summaries
- Source and destination regs where available
- Gadget count or emitted span

Do NOT reconstruct this manually from scattered logs if the sidecar already gives it.

### FLIGHT RECORDER RULE

Assume the ring backend is the default debug backend.

For runtime failures:
- Prefer ring backend
- Dump on fault
- Decode artifacts after the run

Do NOT use `stderr` as the primary mechanism for deep debugging unless:
- The event volume is tiny
- Or you are doing a quick sanity check

The ring buffer plus offline decode is the source of truth. Text output is secondary.

### WHEN YOU ARE ALLOWED TO EXTEND TRACING

You may extend the tracing subsystem only if ALL of these are true:

1. The current bug cannot be resolved using existing events, filters, sidecars, and fixtures
2. The missing visibility is generic and reusable
3. The addition belongs in the centralized trace system, not local runtime code
4. The addition can be controlled by existing levels or event filters

If you add a trace event:
- Add it centrally
- Document it
- Wire it cleanly
- Do not add one-off debug prints instead

### PATCHING RULE

Do NOT patch until you can name the first failing layer:

- ISA truth
- Decoder truth
- Generator truth
- Execution truth
- ABI truth
- Runtime boundary

Once the first failing layer is proven:
- Patch exactly one file
- Patch exactly one function if possible
- Do not mix cleanup with unrelated logic changes
- Rerun the same harness and trace flow after patching

### REQUIRED REPORT FORMAT FOR EVERY DEBUG ITERATION

For every meaningful debug result, reply using this structure:

```
### Current failing unit
- one smallest unit only

### Expected truth
- ISA or ABI expectation

### Trace or harness used
- exact harness mode
- exact trace level
- exact filters used

### Observed truth
- concrete result from trace, sidecar, or fixture

### First failing layer
- one layer only

### Exact next step
- one step only
```

If a patch is justified, add:

```
### Exact patch target
- one file
- one function
```

Do NOT give multiple options unless explicitly asked.

### FORBIDDEN BEHAVIORS

**STRICTLY FORBIDDEN:**
- Adding ad hoc `fprintf` or `printk`
- Adding hardcoded PC-specific probes
- Asking to "just run with more logs"
- Skipping directly to full runtime boot if smaller units exist
- Reopening already-proven lower layers without new contradictory evidence
- Patching multiple unrelated files at once
- Claiming completion without build and test evidence
- Claiming a root cause before the first failing layer is proven

### REQUIRED END-OF-TASK VERIFICATION

Before claiming a bug fix is done, you MUST:

1. Rebuild
2. Rerun the relevant harness or fixture
3. Rerun with the same trace settings that proved the bug
4. Confirm the observed failure is gone
5. Confirm no ad hoc diagnostics were introduced
6. State exactly what passed

If build or tests were not rerun, you MUST say so explicitly.

### IF YOU START DRIFTING

If you catch yourself doing any of these:
- Adding local prints
- Broadening scope
- Re-debugging already-proven units
- Proposing a patch before proving the layer
- Using full runtime traces when a microtest would do

Stop and reset back to:
- Smallest failing unit
- Harness mode selection
- Trace level 2 boundary first
- Filtered capture
- Sidecar inspection if block-related

### OPERATING SUMMARY

Your default behavior from now on is:

- Smallest unit first
- Harness before source edit
- Trace filters before verbosity
- Ring backend before stderr
- Sidecar before manual reconstruction
- One proven failing layer before patching
- One patch target only
- Regression after fix

Follow this literally.
