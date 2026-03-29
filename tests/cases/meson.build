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

# Phase 02b: iOS Simulator Harness (APPSIM-001 through APPSIM-006)
test('case:APPSIM-001', ios_app_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02b-ios-simulator-harness/APPSIM-001-xcodebuildmcp-availability/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/APPSIM-001'],
     suite: ['cases', 'phase-02b-ios-simulator-harness', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:APPSIM-002', ios_app_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02b-ios-simulator-harness/APPSIM-002-simulator-target-selection-and-boot/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/APPSIM-002'],
     suite: ['cases', 'phase-02b-ios-simulator-harness'],
     timeout: 120,
     is_parallel: false)

test('case:APPSIM-003', ios_app_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02b-ios-simulator-harness/APPSIM-003-build-install-launch-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/APPSIM-003'],
     suite: ['cases', 'phase-02b-ios-simulator-harness', 'gate'],
     timeout: 300,
     is_parallel: false)

test('case:APPSIM-004', ios_app_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02b-ios-simulator-harness/APPSIM-004-log-harvest-and-milestone-capture/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/APPSIM-004'],
     suite: ['cases', 'phase-02b-ios-simulator-harness'],
     timeout: 180,
     is_parallel: false)

test('case:APPSIM-005', ios_app_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02b-ios-simulator-harness/APPSIM-005-crash-signature-normalization/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/APPSIM-005'],
     suite: ['cases', 'phase-02b-ios-simulator-harness'],
     timeout: 60,
     is_parallel: false)

test('case:APPSIM-006', ios_app_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02b-ios-simulator-harness/APPSIM-006-bounded-reset-and-relaunch/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/APPSIM-006'],
     suite: ['cases', 'phase-02b-ios-simulator-harness'],
     timeout: 300,
     is_parallel: false)

# Phase 02c: iOS App Runtime Entry (APP-001 through APP-005)
test('case:APP-001', ios_app_harness,
     args: ['--case-yaml', meson.current_source_dir() / '02c-ios-app-runtime-entry/APP-001-boot-to-first-elf-exec/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/APP-001'],
     suite: ['cases', 'phase-02c-ios-app-runtime-entry', 'bootstrap'],
     timeout: 300,
     is_parallel: false)

# EXEC-011: Real TCTI gadget execution
test('case:EXEC-011', semantic_micro_harness,
  args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-011-str-execution/case.yaml',
         '--artifact-dir', artifact_base / 'artifacts/EXEC-011'],
  suite: ['cases', 'phase-03-semantic-exec', 'bootstrap'],
  timeout: 300,
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
     args: ['--case-yaml', meson.current_source_dir() / '03-semantic-exec/EXEC-004-ldr-postindex/case.yaml',
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

test('case:MMU-002', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/MMU-002-mprotect-permissions/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MMU-002'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:MMU-003', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/MMU-003-brk-heap-growth/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MMU-003'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:ABI-001', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/ABI-001-process-entry-stack/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ABI-001'],
     suite: ['cases', 'phase-04-mmu-abi', 'bootstrap'],
     timeout: 120,
     is_parallel: false)

test('case:MMU-004', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/MMU-004-growdown-stack/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MMU-004'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:ABI-002', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/ABI-002-required-auxv/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ABI-002'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:ABI-003', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/ABI-003-at-random/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ABI-003'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:MMU-005', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/MMU-005-cross-page-access/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MMU-005'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:MMU-006', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/MMU-006-tlb-invalidation/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MMU-006'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:ABI-004', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/ABI-004-initial-tpidr-el0/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ABI-004'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:ABI-005', abi_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '04-mmu-abi/ABI-005-signal-frame-rt-sigreturn/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ABI-005'],
     suite: ['cases', 'phase-04-mmu-abi'],
     timeout: 60,
     is_parallel: false)

test('case:ELF-001', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-001-static-hello/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-001'],
     suite: ['cases', 'phase-05-elf-loader', 'bootstrap'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-002', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-002-static-pie-hello/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-002'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-003', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-003-dynamic-pie-hello/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-003'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-004', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-004-pt-load-mapping/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-004'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-005', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-005-relocations-basic/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-005'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-006', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-006-interpreter-handoff/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-006'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-007', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-007-musl-loader-entry/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-007'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-008', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-008-glibc-loader-entry/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-008'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

test('case:ELF-009', elf_loader_harness,
     args: ['--case-yaml', meson.current_source_dir() / '05-elf-loader/ELF-009-init-array-fini-array/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/ELF-009'],
     suite: ['cases', 'phase-05-elf-loader'],
     timeout: 300,
     is_parallel: false)

# Phase 06: System Calls - Core, Filesystem, Network
test('case:SYS-001', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-001-read-write-close/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-001'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-002', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-002-openat-fstat-lseek/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-002'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-003', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-003-getdents64/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-003'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-004', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-004-mmap-munmap-mprotect/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-004'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-005', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-005-brk/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-005'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-006', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-006-dup-pipe-pipe2/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-006'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-007', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-007-ioctl-tty-pty-minimum/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-007'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-008', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-008-getrandom-prctl-uname/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-008'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-009', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-009-poll-ppoll-select-epoll/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-009'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-010', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-010-socket-connect-accept/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-010'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-011', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-011-send-recv-msg/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-011'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

test('case:SYS-012', syscall_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '06-syscalls-core-fs-net/SYS-012-rename-link-symlink-readlink/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SYS-012'],
     suite: ['cases', 'phase-06-syscalls-core-fs-net'],
     timeout: 60,
     is_parallel: false)

# Phase 07: Threads, Signals, and TLS
test('case:THR-001', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/THR-001-clone-thread-start/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/THR-001'],
     suite: ['cases', 'phase-07-threads-signals-tls', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:THR-002', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/THR-002-set-tid-address/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/THR-002'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

test('case:THR-003', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/THR-003-set-robust-list/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/THR-003'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

test('case:THR-004', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/THR-004-futex-wait-wake/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/THR-004'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

test('case:THR-005', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/THR-005-pthread-mutex-condvar/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/THR-005'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

test('case:THR-006', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/THR-006-thread-local-storage/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/THR-006'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

test('case:SIG-001', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/SIG-001-rt-sigaction-mask/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SIG-001'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

test('case:SIG-002', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/SIG-002-signal-delivery/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SIG-002'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

test('case:SIG-003', thread_signal_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '07-threads-signals-tls/SIG-003-sa-restart-and-fatal-signal/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/SIG-003'],
     suite: ['cases', 'phase-07-threads-signals-tls'],
     timeout: 60,
     is_parallel: false)

# Phase 08: musl libc Compatibility
test('case:MUSL-001', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-001-static-busybox-true/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-001'],
     suite: ['cases', 'phase-08-musl', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:MUSL-002', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-002-dynamic-busybox-true/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-002'],
     suite: ['cases', 'phase-08-musl'],
     timeout: 60,
     is_parallel: false)

test('case:MUSL-003', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-003-dynamic-busybox-sh/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-003'],
     suite: ['cases', 'phase-08-musl'],
     timeout: 60,
     is_parallel: false)

test('case:MUSL-004', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-004-coreutils-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-004'],
     suite: ['cases', 'phase-08-musl'],
     timeout: 60,
     is_parallel: false)

test('case:MUSL-005', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-005-apk-update/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-005'],
     suite: ['cases', 'phase-08-musl'],
     timeout: 60,
     is_parallel: false)

test('case:MUSL-006', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-006-apk-install-tiny-package/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-006'],
     suite: ['cases', 'phase-08-musl'],
     timeout: 60,
     is_parallel: false)

test('case:MUSL-007', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-007-musl-pthread-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-007'],
     suite: ['cases', 'phase-08-musl'],
     timeout: 60,
     is_parallel: false)

test('case:MUSL-008', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '08-musl/MUSL-008-musl-network-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/MUSL-008'],
     suite: ['cases', 'phase-08-musl'],
     timeout: 60,
     is_parallel: false)

# Phase 09: glibc Compatibility
test('case:GLIBC-001', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-001-dynamic-hello/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-001'],
     suite: ['cases', 'phase-09-glibc', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-002', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-002-bin-true/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-002'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-003', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-003-bin-sh/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-003'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-004', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-004-bash-startup/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-004'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-005', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-005-coreutils-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-005'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-006', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-006-apt-update/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-006'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-007', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-007-glibc-pthread-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-007'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-008', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-008-python-startup/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-008'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-009', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-009-libstdcxx-unwind-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-009'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

test('case:GLIBC-010', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '09-glibc/GLIBC-010-curl-https-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/GLIBC-010'],
     suite: ['cases', 'phase-09-glibc'],
     timeout: 60,
     is_parallel: false)

# Phase 10: Tooling, Stability, and iOS Integration
test('case:TOOL-001', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/TOOL-001-gcc-or-clang-guest-build/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TOOL-001'],
     suite: ['cases', 'phase-10-tooling-stability-ios', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:TOOL-002', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/TOOL-002-make-cmake-basic/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TOOL-002'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 60,
     is_parallel: false)

test('case:TOOL-003', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/TOOL-003-git-clone-status/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TOOL-003'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 60,
     is_parallel: false)

test('case:TOOL-004', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/TOOL-004-tar-xz-zstd-smoke/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/TOOL-004'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 60,
     is_parallel: false)

test('case:STAB-001', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/STAB-001-shell-loop-stability/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/STAB-001'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 120,
     is_parallel: false)

test('case:STAB-002', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/STAB-002-fork-exec-storm/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/STAB-002'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 120,
     is_parallel: false)

test('case:STAB-003', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/STAB-003-thread-storm/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/STAB-003'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 120,
     is_parallel: false)

test('case:IOS-001', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/IOS-001-execution-strategy-sandbox/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/IOS-001'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 60,
     is_parallel: false)

test('case:IOS-002', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/IOS-002-suspend-resume/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/IOS-002'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 60,
     is_parallel: false)

test('case:IOS-003', ios_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '10-tooling-stability-ios/IOS-003-trace-artifact-export/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/IOS-003'],
     suite: ['cases', 'phase-10-tooling-stability-ios'],
     timeout: 60,
     is_parallel: false)

# Phase 11: Distribution Matrix
test('case:DISTRO-001', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '11-distro-matrix/DISTRO-001-alpine-minimal/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DISTRO-001'],
     suite: ['cases', 'phase-11-distro-matrix', 'bootstrap'],
     timeout: 60,
     is_parallel: false)

test('case:DISTRO-002', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '11-distro-matrix/DISTRO-002-alpine-package-manager/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DISTRO-002'],
     suite: ['cases', 'phase-11-distro-matrix'],
     timeout: 60,
     is_parallel: false)

test('case:DISTRO-003', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '11-distro-matrix/DISTRO-003-ubuntu-minimal/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DISTRO-003'],
     suite: ['cases', 'phase-11-distro-matrix'],
     timeout: 60,
     is_parallel: false)

test('case:DISTRO-004', distro_fixture_harness,
     args: ['--case-yaml', meson.current_source_dir() / '11-distro-matrix/DISTRO-004-ubuntu-package-manager/case.yaml',
            '--artifact-dir', artifact_base / 'artifacts/DISTRO-004'],
     suite: ['cases', 'phase-11-distro-matrix'],
     timeout: 60,
     is_parallel: false)
