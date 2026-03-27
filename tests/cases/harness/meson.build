# Harness binaries for case system
# Each harness reads case.yaml and produces artifacts

decode_golden_harness = executable('decode_golden',
    'decode_golden.c',
    include_directories: [include_directories('../../..')],
    link_with: [libish_emu],
    install: false
)

generator_golden_harness = executable('generator_golden',
    'generator_golden.c',
    include_directories: [include_directories('../../..')],
    link_with: [libish_emu],
    install: false
)

semantic_micro_harness = executable('semantic_micro',
    'semantic_micro.c',
    include_directories: [include_directories('../../..')],
    link_with: [libish_emu],
    install: false
)

abi_fixture_harness = executable('abi_fixture',
    'abi_fixture.c',
    include_directories: [include_directories('../../..')],
    link_with: [libish_emu],
    install: false
)

runtime_trace_harness = executable('runtime_trace',
    'runtime_trace.c',
    include_directories: [include_directories('../../..')],
    link_with: [libish_emu],
    install: false
)

elf_loader_harness = executable('elf_loader',
    'elf_loader.c',
    include_directories: [include_directories('../../..')],
    link_with: [libish_emu],
    install: false
)
