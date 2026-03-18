#
# aarch64-specific build configuration for iSH
# Include this in the main meson.build when arch=aarch64
#

# aarch64 architecture define
add_project_arguments('-DARCH_AARCH64=1', language: 'c')

# aarch64-specific emulator sources
aarch64_emu_src = [
    'emu/aarch64/decode.c',
    'emu/aarch64/tls.c',
]

# aarch64-specific kernel sources
aarch64_kernel_src = [
    'kernel/aarch64/signal.c',
    'kernel/aarch64/calls.h',
    'emu/aarch64/memory.h',
    'emu/aarch64/tls.h',
]

# TCTI gadget sources
# These are pre-generated C files with naked functions
tcti_gadget_src = [
    'asbestos/aarch64/gadgets_tcti_impl.c',
]

# aarch64 gadget headers (no assembly, pure C with inline asm)
aarch64_gadget_src = [
    'asbestos/aarch64/gadgets_entry.c',
    'asbestos/aarch64/gadgets_arith.c',
]

# Generate TCTI gadgets if needed
# This runs the Python script to generate the large gadget tables
tcti_gadget_gen = custom_target('tcti_gadgets',
    output: ['tcti_basic_gadgets.c', 'tcti_basic_gadgets.h'],
    input: 'asbestos/aarch64/tcti-gadget-gen.py',
    command: [python, '@INPUT@'],
    build_by_default: false,
)

# Offsets for aarch64 CPU state
aarch64_offsets = custom_target('aarch64_offsets',
    output: 'aarch64-cpu-offsets.h',
    input: 'asbestos/aarch64/offsets.c',
    depfile: 'aarch64-cpu-offsets.h.d',
    command: [find_program('tools/staticdefine.sh'), '@OUTDIR@/compile_commands.json', '@INPUT@', '@OUTPUT@', '@DEPFILE@'],
)

# Combine all aarch64 sources
aarch64_all_src = aarch64_emu_src + aarch64_kernel_src + tcti_gadget_src + aarch64_gadget_src

# Dependencies for aarch64
# No special dependencies beyond standard ones
