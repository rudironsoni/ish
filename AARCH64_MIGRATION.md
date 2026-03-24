# AArch64-Only Branch

**Status**: This branch is AArch64 guest only.

## Architecture

This branch supports only AArch64 (64-bit ARM) Linux emulation using the TCTI (Threaded Code Translation and Interpretation) execution engine.

## Execution Engine

**TCTI Only**: This branch uses only the TCTI execution engine.

## Build

```bash
meson setup build
ninja -C build
```

There is no architecture selection option - AArch64 is the only supported guest architecture.

## Source Tree

- `emu/aarch64/` - AArch64 CPU emulation
- `tcti/aarch64/` - TCTI gadget generation and execution
- `kernel/` - Architecture-neutral kernel interface
- `fs/` - Architecture-neutral filesystem layer

## Testing

```bash
# Run unit tests
meson test -C build

# Run decoder tests
gcc -I. tests/aarch64/decoder_test.c emu/aarch64/decode.c -o /tmp/decoder_test && /tmp/decoder_test
```

## References

- [ARMv8 Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487)
- [Linux AArch64 Signal Context](https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/sigcontext.h)
