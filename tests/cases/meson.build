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

test('case_TRACE-005', runtime_trace_harness,
     args: ['--case-yaml', meson.current_source_dir() / '00-trace-harness/TRACE-005/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TRACE-005'],
     suite: ['cases', 'phase-00-trace-harness'],
     timeout: 60,
     is_parallel: false)

test('case_TRACE-006', runtime_trace_harness,
     args: ['--case-yaml', meson.current_source_dir() / '00-trace-harness/TRACE-006-offline-decode/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TRACE-006'],
     suite: ['cases', 'phase-00-trace-harness'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-001', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-001-addsub-immediate/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-001'],
     suite: ['cases', 'phase-01-decode', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-002', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-002-addsub-shifted-register/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-002'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-003', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-003-logical-immediate-and-register/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-003'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-004', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-004-unconditional-branch/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-004'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-005', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-005-conditional-branch-family/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-005'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-006', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-006-cbz-cbnz-tbz-tbnz/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-006'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-007', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-007-load-store-instructions/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-007'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-008', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-008-move-wide-immediate/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-008'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-009', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-009-bitfield-move/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-009'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-010', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-010-extract-and-branch-reg/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-010'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:DEC-011', decode_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '01-decode/DEC-011-system-and-barrier/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DEC-011'],
     suite: ['cases', 'phase-01-decode'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-001', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-001-simple-alu-emission/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-001'],
     suite: ['cases', 'phase-02-generator', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-002', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-002-flag-setting-compare/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-002'],
     suite: ['cases', 'phase-02-generator'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-003', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-003-post-index-store-emission/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-003'],
     suite: ['cases', 'phase-02-generator'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-004', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-004-cbz-emission/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-004'],
     suite: ['cases', 'phase-02-generator'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-005', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-005-bcond-emission/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-005'],
     suite: ['cases', 'phase-02-generator'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-006', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-006-ldp-stp-emission/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-006'],
     suite: ['cases', 'phase-02-generator'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-007', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-007-system-register-emission/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-007'],
     suite: ['cases', 'phase-02-generator'],
     timeout: 60,
     is_parallel: false)

test('case:GEN-008', generator_golden_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02-generator/GEN-008-block-sidecar-map/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GEN-008'],
     suite: ['cases', 'phase-02-generator'],
     timeout: 60,
     is_parallel: false)

test('case:EXEC-001', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-001-single-alu/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-001'],
     suite: ['cases', 'phase-03-semantic-exec', 'bootstrap'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-002', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-002-cmp-flags/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-002'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-003', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-003-str-postindex/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-003'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-004', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-004-str-cmp-bne-loop/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-004'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-005', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-005-hot-register-sync/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-005'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-006', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-006-fast-vs-helper-equivalence/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-006'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-007', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-007-block-save-restore/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-007'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-008', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-008-next-pc-selection/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-008'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-009', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-009-svc-entry/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-009'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:EXEC-010', semantic_micro_harness,
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-010-fault-address-propagation/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/EXEC-010'],
     suite: ['cases', 'phase-03-semantic-exec'],
     timeout: 120,
     is_parallel: false)

test('case:MMU-001', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/MMU-001-anon-mmap/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MMU-001'],
     suite: ['cases', 'phase-04-mmu-abi', 'bootstrap'],
     timeout: 60,
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
