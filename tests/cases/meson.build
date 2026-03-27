# Case system - explicit test declarations only
# No dynamic discovery. Cases are added one by one.

subdir('harness')

# Get build directory for artifacts
artifact_base = meson.current_build_dir()

# EXPLICIT test declarations - bootstrap set
# Each test references an already-built harness target

test('case_TRACE-001', runtime_trace_harness,
     args: ['--case-yaml', meson.current_source_dir() / '00-trace-harness/TRACE-001-boundary-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TRACE-001'],
     suite: ['cases', 'phase-00-trace-harness', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case_TRACE-002', runtime_trace_harness,
     args: ['--case-yaml', meson.current_source_dir() / '00-trace-harness/TRACE-002/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TRACE-002'],
     suite: ['cases', 'phase-00-trace-harness'],
     timeout: 60,
     is_parallel: false)

test('case_TRACE-003', runtime_trace_harness,
     args: ['--case-yaml', meson.current_source_dir() / '00-trace-harness/TRACE-003/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TRACE-003'],
     suite: ['cases', 'phase-00-trace-harness'],
     timeout: 60,
     is_parallel: false)

test('case_TRACE-004', runtime_trace_harness,
     args: ['--case-yaml', meson.current_source_dir() / '00-trace-harness/TRACE-004/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TRACE-004'],
     suite: ['cases', 'phase-00-trace-harness'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-001', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-001-addsub-immediate/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-001'],
     suite: ['cases', 'phase-01-decode', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-001', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-001-simple-alu-emission/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-001'],
     suite: ['cases', 'phase-02-generator', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:EXEC-001', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-001-single-alu/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-001'],
     suite: ['cases', 'phase-03-semantic-exec', 'bootstrap'],
     timeout: 120,
     is_parallel: false)

test('case:ABI-001', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/ABI-001-process-entry-stack/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ABI-001'],
     suite: ['cases', 'phase-04-mmu-abi', 'bootstrap'],
     timeout: 120,
     is_parallel: false)

test('case:ELF-001', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-001-static-hello/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-001'],
     suite: ['cases', 'phase-05-elf-loader', 'bootstrap'],
     timeout: 300,
     is_parallel: false)
