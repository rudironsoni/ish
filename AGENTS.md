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

## 📚 Resources
- README.md: General project information and build instructions
- Wiki: https://github.com/ish-app/ish/wiki (detailed documentation)
- Issues: https://github.com/ish-app/ish/issues (bug tracking and feature requests)
- TEST_RESULTS.md: Recent test outcomes

This guide should help you navigate and contribute effectively to the iSH codebase. When in doubt, follow the existing patterns in the code you're modifying.