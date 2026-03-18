# aarch64 Migration for iSH

This document tracks the progress of migrating iSH from x86 emulation to aarch64 Linux emulation.

## Architecture Overview

iSH now supports dual-architecture emulation:
- **x86 (i386)**: Original implementation, 32-bit x86 Linux
- **aarch64**: New implementation, 64-bit ARM Linux

Build with: `meson setup build -Darch=aarch64`

## Current Status

### ✅ Completed

| Component | Status | Notes |
|-----------|--------|-------|
| CPU State | ✅ Complete | `emu/aarch64/cpu.h` - 31 x regs, sp, pc, pstate, vregs[32], tpidr_el0 |
| Instruction Decoder | ✅ Complete | `emu/aarch64/decode.c` - Fixed 32-bit instruction decoding |
| TCTI Gadgets | ✅ Complete | 20,760 naked assembly functions |
| Block Generator | ✅ Complete | `asbestos/aarch64/gen.c` - Emits gadget chains |
| Syscall Table | ✅ Complete | `kernel/aarch64/calls.h` - 446 aarch64 syscall numbers |
| Signal Handling | ✅ Complete | `kernel/aarch64/signal.c` - Linux-compatible sigcontext |
| TLS Support | ✅ Complete | `emu/aarch64/tls.c` - TPIDR_EL0 emulation |
| Build System | ✅ Complete | `meson.build` - Dual-architecture support |
| ELF Loading | ✅ Complete | `kernel/exec.c` - 64-bit ELF support |
| Tests | ✅ Partial | Unit + integration tests passing |

### 🚧 In Progress

| Component | Status | Notes |
|-----------|--------|-------|
| Load/Store Gadgets | 🚧 Missing | Need memory access TCTI gadgets |
| Full E2E Tests | 🚧 Missing | Need aarch64 static binaries |
| VDSO | 🚧 Missing | aarch64 vdso page for signals |

### 📋 Files Changed

```
emu/aarch64/
├── cpu.h              # aarch64 CPU state structure
├── decode.c           # Instruction decoder
├── decode.h           # Decoder API
├── tls.c              # TLS/TPIDR_EL0 handling
└── tls.h              # TLS API

asbestos/aarch64/
├── gen.c              # TCTI block generator
├── gen.h              # Generator API
├── gadgets_tcti.h     # Gadget declarations
├── gadgets_tcti_impl.c # 20,760 generated gadgets
├── gadgets_entry.c    # Entry/exit helpers
├── gadgets_arith.c    # Flag-setting operations
└── tcti-gadget-gen.py # Gadget generator script

kernel/aarch64/
├── signal.h           # Signal frame structures
├── signal.c           # Signal delivery/return
└── calls.h            # 446 syscall numbers

kernel/
├── elf.h              # Updated for 64-bit ELF
├── exec.c             # Dual-arch ELF loading
└── calls.c            # Dual-arch syscall handling

meson.build            # Dual-architecture build
meson_options.txt      # arch option added
```

## Testing

### Unit Tests

```bash
# Generator state tests
gcc -I. tests/aarch64/gen_test_simple.c -o /tmp/gen_test && /tmp/gen_test
# Expected: 6/6 passing

# Integration tests (with mock gadgets)
gcc -I. tests/aarch64/integration_test_simple.c \
    tests/aarch64/gen_test_minimal.c \
    emu/aarch64/decode.c -o /tmp/integration_test
/tmp/integration_test
# Expected: 10/10 passing
```

### Build Tests

```bash
# Configure for aarch64
meson setup build_aarch64 -Darch=aarch64
ninja -C build_aarch64

# Configure for x86 (default)
meson setup build_x86 -Darch=x86
ninja -C build_x86
```

## TCTI Gadget System

The aarch64 emulator uses Threaded Code Translation and Interpretation (TCTI):

### Register Mapping

| Guest | Host | Notes |
|-------|------|-------|
| x0-x15 | x1-x16 | Direct register mapping |
| x16-x30 | Memory | Memory-backed via cpu->x[] |
| SP | x17 + mem | Stack pointer |
| x28 | - | Bytecode stream pointer |
| x29 | - | CPU state pointer |
| x27 | - | Temporary / link register |

### Gadget Count

| Operation | Count | Description |
|-----------|-------|-------------|
| ADD reg | 4,096 | `add xRd, xRn, xRm` (16³ combinations) |
| SUB reg | 4,096 | `sub xRd, xRn, xRm` (16³ combinations) |
| AND reg | 4,096 | `and xRd, xRn, xRm` (16³ combinations) |
| ORR reg | 4,096 | `orr xRd, xRn, xRm` (16³ combinations) |
| EOR reg | 4,096 | `eor xRd, xRn, xRm` (16³ combinations) |
| MOV reg | 256 | `mov xRd, xRn` (16² combinations) |
| MOV imm | 16 | `movz xRd, #imm` (16 values) |
| Branch | 5 | `b`, `b.cond`, `cbz`, `cbnz`, `br` |
| System | 4 | `svc`, `mrs`, `msr`, `nop` |
| **Total** | **~20,760** | Pre-generated naked functions |

### Gadget Example

```c
// ADD x0, x1, x2
__attribute__((naked)) void gadget_add_reg_0_1_2(void) {
    asm volatile(
        "add x1, x2, x3\n\t"     // Guest x0,x1,x2 → Host x1,x2,x3
        "ldr x27, [x28], #8\n\t" // Load next gadget address
        "br x27\n\t"             // Jump to next gadget
    );
}
```

## Syscall ABI

### aarch64
- **Syscall number**: x8
- **Arguments**: x0, x1, x2, x3, x4, x5
- **Return value**: x0

### x86
- **Syscall number**: eax
- **Arguments**: ebx, ecx, edx, esi, edi, ebp
- **Return value**: eax

## Signal Frame Layout

Matches Linux aarch64 signal frames:

```c
struct a64_rt_sigframe {
    struct a64_siginfo info;      // Signal info
    struct a64_ucontext uc;         // User context
    struct a64_fpsimd_context fp;   // FP/SIMD state
    struct a64_frame_record fr;     // Frame record for unwinding
};

struct a64_sigcontext {
    uint64_t fault_address;         // Fault address for SIGSEGV
    uint64_t regs[31];              // x0-x30
    uint64_t sp;                    // Stack pointer
    uint64_t pc;                    // Program counter
    uint64_t pstate;                // Processor state (NZCV + DAIF)
    uint8_t __reserved[4096];       // Extended contexts (FPSIMD, etc.)
};
```

## Next Steps

1. **Load/Store Gadgets**: Implement memory access with TLB translation
2. **VDSO**: Create aarch64 vdso page with sigtramp
3. **E2E Testing**: Compile aarch64 static binaries and test
4. **Optimization**: Profile and optimize gadget chains

## References

- [UTM QEMU TCTI](https://github.com/utmapp/qemu) - Reference implementation
- [ARMv8 Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487)
- [Linux aarch64 Signal Context](https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/sigcontext.h)
