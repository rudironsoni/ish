#
# aarch64-specific build configuration for iSH
# Include this in the main meson.build when arch=aarch64
#

# aarch64 architecture define
add_project_arguments('-DARCH_AARCH64=1', language: 'c')
add_project_arguments('-DENGINE_AARCH64=1', language: 'c')

# Generate TCTI gadgets if the implementation doesn't exist
# This runs the Python script to generate the large gadget tables
tcti_gadget_gen = custom_target('tcti_gadgets',
    output: ['gadgets_tcti.h', 'gadgets_tcti_impl.c'],
    input: 'asbestos/aarch64/tcti-gadget-gen.py',
    command: [find_program('python3'), '@INPUT@', '-o', 'asbestos/aarch64'],
    build_by_default: true,
)

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

# TCTI gadget sources - pre-generated C files with naked functions
# Only include the generated implementation, not the generator script
aarch64_gadget_src = [
    'asbestos/aarch64/gadgets_entry.c',
    'asbestos/aarch64/gadgets_arith.c',
    'asbestos/aarch64/gen.c',
    tcti_gadget_gen,
]

# Offsets for aarch64 CPU state
aarch64_offsets = custom_target('aarch64_offsets',
    output: 'aarch64-cpu-offsets.h',
    input: 'asbestos/aarch64/offsets.c',
    depfile: 'aarch64-cpu-offsets.h.d',
    command: [find_program('tools/staticdefine.sh'), '@OUTDIR@/compile_commands.json', '@INPUT@', '@OUTPUT@', '@DEPFILE@'],
)

# Combine all aarch64 sources
aarch64_all_src = aarch64_emu_src + aarch64_kernel_src + aarch64_gadget_src

# Dependencies for aarch64
# No special dependencies beyond standard ones
