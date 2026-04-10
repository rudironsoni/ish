import XCTest

@_silgen_name("run_decode_golden")
private func run_decode_golden(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_decode_golden_case")
private func run_decode_golden_case(_ case_id: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_semantic_micro")
private func run_semantic_micro(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_abi_fixture")
private func run_abi_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_runtime_trace")
private func run_runtime_trace(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_syscall_fixture")
private func run_syscall_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_elf_loader")
private func run_elf_loader(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_distro_fixture")
private func run_distro_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_thread_signal_fixture")
private func run_thread_signal_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

struct CaseRecord {
    let caseId: String
    let phase: String
    let harnessType: String
    let caseYamlRelativePath: String
}

final class IXLandLinuxRuntimeFunctionalTests: XCTestCase {
    private static let allCases: [CaseRecord] = [
        CaseRecord(caseId: "DEC-001", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-001-addsub-immediate/case.yaml"),
        CaseRecord(caseId: "DEC-002", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-002-addsub-shifted-register/case.yaml"),
        CaseRecord(caseId: "DEC-003", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-003-logical-immediate-and-register/case.yaml"),
        CaseRecord(caseId: "DEC-004", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-004-unconditional-branch/case.yaml"),
        CaseRecord(caseId: "DEC-005", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-005-conditional-branch-family/case.yaml"),
        CaseRecord(caseId: "DEC-006", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-006-cbz-cbnz-tbz-tbnz/case.yaml"),
        CaseRecord(caseId: "DEC-007", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-007-ldr-str-addressing-modes/case.yaml"),
        CaseRecord(caseId: "DEC-007", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-007-load-store-instructions/case.yaml"),
        CaseRecord(caseId: "DEC-008", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-008-ldp-stp/case.yaml"),
        CaseRecord(caseId: "DEC-008", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-008-move-wide-immediate/case.yaml"),
        CaseRecord(caseId: "DEC-009", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-009-bitfield-move/case.yaml"),
        CaseRecord(caseId: "DEC-009", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-009-system-registers-tpidr/case.yaml"),
        CaseRecord(caseId: "DEC-010", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-010-extract-and-branch-reg/case.yaml"),
        CaseRecord(caseId: "DEC-010", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-010-svc-brk-udf/case.yaml"),
        CaseRecord(caseId: "DEC-011", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-011-fp-simd-core/case.yaml"),
        CaseRecord(caseId: "DEC-011", phase: "01-decode", harnessType: "decode_golden", caseYamlRelativePath: "cases/01-decode/DEC-011-system-and-barrier/case.yaml"),
        CaseRecord(caseId: "EXEC-001", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-001-single-alu/case.yaml"),
        CaseRecord(caseId: "EXEC-002", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-002-cmp-flags/case.yaml"),
        CaseRecord(caseId: "EXEC-003", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-003-str-postindex/case.yaml"),
        CaseRecord(caseId: "EXEC-004", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-004-ldr-postindex/case.yaml"),
        CaseRecord(caseId: "EXEC-004", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-004-str-cmp-bne-loop/case.yaml"),
        CaseRecord(caseId: "EXEC-005", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-005-hot-register-sync/case.yaml"),
        CaseRecord(caseId: "EXEC-006", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-006-fast-vs-helper-equivalence/case.yaml"),
        CaseRecord(caseId: "EXEC-007", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-007-block-save-restore/case.yaml"),
        CaseRecord(caseId: "EXEC-008", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-008-next-pc-selection/case.yaml"),
        CaseRecord(caseId: "EXEC-009", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-009-svc-entry/case.yaml"),
        CaseRecord(caseId: "EXEC-010", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-010-fault-address-propagation/case.yaml"),
        CaseRecord(caseId: "EXEC-011", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-011-str-execution/case.yaml"),
        CaseRecord(caseId: "EXEC-012", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-012-cmp-equality-flags/case.yaml"),
        CaseRecord(caseId: "EXEC-013", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-013-cmp-inequality-flags/case.yaml"),
        CaseRecord(caseId: "EXEC-014", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-014-bne-fallthrough-when-z-set/case.yaml"),
        CaseRecord(caseId: "EXEC-015", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-015-bne-taken-when-z-clear/case.yaml"),
        CaseRecord(caseId: "EXEC-016", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-016-msr-mrs-nzcv-roundtrip/case.yaml"),
        CaseRecord(caseId: "EXEC-017", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-017-tbz-bit30-branch/case.yaml"),
        CaseRecord(caseId: "EXEC-018", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-018-ubfx-bit30-extraction/case.yaml"),
        CaseRecord(caseId: "EXEC-019", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-019-str-xzr-postindex/case.yaml"),
        CaseRecord(caseId: "EXEC-020", phase: "03-semantic-exec", harnessType: "semantic_micro", caseYamlRelativePath: "cases/03-semantic-exec/EXEC-020-branch-path-consumption/case.yaml"),
        CaseRecord(caseId: "ABI-001", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/ABI-001-process-entry-stack/case.yaml"),
        CaseRecord(caseId: "ABI-002", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/ABI-002-required-auxv/case.yaml"),
        CaseRecord(caseId: "ABI-003", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/ABI-003-at-random/case.yaml"),
        CaseRecord(caseId: "ABI-004", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/ABI-004-initial-tpidr-el0/case.yaml"),
        CaseRecord(caseId: "ABI-005", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/ABI-005-signal-frame-rt-sigreturn/case.yaml"),
        CaseRecord(caseId: "ABI-009", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/ABI-009-startup-stack-address-containment/case.yaml"),
        CaseRecord(caseId: "MMU-001", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/MMU-001-anon-mmap/case.yaml"),
        CaseRecord(caseId: "MMU-002", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/MMU-002-mprotect-permissions/case.yaml"),
        CaseRecord(caseId: "MMU-003", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/MMU-003-brk-heap-growth/case.yaml"),
        CaseRecord(caseId: "MMU-004", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/MMU-004-growdown-stack/case.yaml"),
        CaseRecord(caseId: "MMU-005", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/MMU-005-cross-page-access/case.yaml"),
        CaseRecord(caseId: "MMU-006", phase: "04-mmu-abi", harnessType: "abi_fixture", caseYamlRelativePath: "cases/04-mmu-abi/MMU-006-tlb-invalidation/case.yaml"),
        CaseRecord(caseId: "ELF-001", phase: "05-elf-loader", harnessType: "runtime_trace", caseYamlRelativePath: "cases/05-elf-loader/ELF-001-static-hello/case.yaml"),
        CaseRecord(caseId: "ELF-002", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-002-static-pie-hello/case.yaml"),
        CaseRecord(caseId: "ELF-003", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-003-dynamic-pie-hello/case.yaml"),
        CaseRecord(caseId: "ELF-004", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-004-pt-load-mapping/case.yaml"),
        CaseRecord(caseId: "ELF-005", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-005-relocations-basic/case.yaml"),
        CaseRecord(caseId: "ELF-006", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-006-interpreter-handoff/case.yaml"),
        CaseRecord(caseId: "ELF-007", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-007-musl-loader-entry/case.yaml"),
        CaseRecord(caseId: "ELF-008", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-008-glibc-loader-entry/case.yaml"),
        CaseRecord(caseId: "ELF-009", phase: "05-elf-loader", harnessType: "elf_fixture", caseYamlRelativePath: "cases/05-elf-loader/ELF-009-init-array-fini-array/case.yaml"),
        CaseRecord(caseId: "SYS-001", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-001-read-write-close/case.yaml"),
        CaseRecord(caseId: "SYS-002", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-002-openat-fstat-lseek/case.yaml"),
        CaseRecord(caseId: "SYS-003", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-003-getdents64/case.yaml"),
        CaseRecord(caseId: "SYS-004", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-004-mmap-munmap-mprotect/case.yaml"),
        CaseRecord(caseId: "SYS-005", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-005-brk/case.yaml"),
        CaseRecord(caseId: "SYS-006", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-006-dup-pipe-pipe2/case.yaml"),
        CaseRecord(caseId: "SYS-007", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-007-ioctl-tty-pty-minimum/case.yaml"),
        CaseRecord(caseId: "SYS-008", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-008-getrandom-prctl-uname/case.yaml"),
        CaseRecord(caseId: "SYS-009", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-009-poll-ppoll-select-epoll/case.yaml"),
        CaseRecord(caseId: "SYS-010", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-010-socket-connect-accept/case.yaml"),
        CaseRecord(caseId: "SYS-011", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-011-send-recv-msg/case.yaml"),
        CaseRecord(caseId: "SYS-012", phase: "06-syscalls-core-fs-net", harnessType: "syscall_fixture", caseYamlRelativePath: "cases/06-syscalls-core-fs-net/SYS-012-rename-link-symlink-readlink/case.yaml"),
        CaseRecord(caseId: "SIG-001", phase: "07-threads-signals-tls", harnessType: "signal_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/SIG-001-rt-sigaction-mask/case.yaml"),
        CaseRecord(caseId: "SIG-002", phase: "07-threads-signals-tls", harnessType: "signal_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/SIG-002-signal-delivery/case.yaml"),
        CaseRecord(caseId: "SIG-003", phase: "07-threads-signals-tls", harnessType: "signal_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/SIG-003-sa-restart-and-fatal-signal/case.yaml"),
        CaseRecord(caseId: "THR-001", phase: "07-threads-signals-tls", harnessType: "thread_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/THR-001-clone-thread-start/case.yaml"),
        CaseRecord(caseId: "THR-002", phase: "07-threads-signals-tls", harnessType: "thread_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/THR-002-set-tid-address/case.yaml"),
        CaseRecord(caseId: "THR-003", phase: "07-threads-signals-tls", harnessType: "thread_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/THR-003-set-robust-list/case.yaml"),
        CaseRecord(caseId: "THR-004", phase: "07-threads-signals-tls", harnessType: "thread_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/THR-004-futex-wait-wake/case.yaml"),
        CaseRecord(caseId: "THR-005", phase: "07-threads-signals-tls", harnessType: "thread_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/THR-005-pthread-mutex-condvar/case.yaml"),
        CaseRecord(caseId: "THR-006", phase: "07-threads-signals-tls", harnessType: "thread_fixture", caseYamlRelativePath: "cases/07-threads-signals-tls/THR-006-thread-local-storage/case.yaml"),
        CaseRecord(caseId: "MUSL-001", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-001-static-busybox-true/case.yaml"),
        CaseRecord(caseId: "MUSL-002", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-002-dynamic-busybox-true/case.yaml"),
        CaseRecord(caseId: "MUSL-003", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-003-dynamic-busybox-sh/case.yaml"),
        CaseRecord(caseId: "MUSL-004", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-004-coreutils-smoke/case.yaml"),
        CaseRecord(caseId: "MUSL-005", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-005-apk-update/case.yaml"),
        CaseRecord(caseId: "MUSL-006", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-006-apk-install-tiny-package/case.yaml"),
        CaseRecord(caseId: "MUSL-007", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-007-musl-pthread-smoke/case.yaml"),
        CaseRecord(caseId: "MUSL-008", phase: "08-musl", harnessType: "distro_fixture", caseYamlRelativePath: "cases/08-musl/MUSL-008-musl-network-smoke/case.yaml"),
        CaseRecord(caseId: "GLIBC-001", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-001-dynamic-hello/case.yaml"),
        CaseRecord(caseId: "GLIBC-002", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-002-bin-true/case.yaml"),
        CaseRecord(caseId: "GLIBC-003", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-003-bin-sh/case.yaml"),
        CaseRecord(caseId: "GLIBC-004", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-004-bash-startup/case.yaml"),
        CaseRecord(caseId: "GLIBC-005", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-005-coreutils-smoke/case.yaml"),
        CaseRecord(caseId: "GLIBC-006", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-006-apt-update/case.yaml"),
        CaseRecord(caseId: "GLIBC-007", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-007-glibc-pthread-smoke/case.yaml"),
        CaseRecord(caseId: "GLIBC-008", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-008-python-startup/case.yaml"),
        CaseRecord(caseId: "GLIBC-009", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-009-libstdcxx-unwind-smoke/case.yaml"),
        CaseRecord(caseId: "GLIBC-010", phase: "09-glibc", harnessType: "distro_fixture", caseYamlRelativePath: "cases/09-glibc/GLIBC-010-curl-https-smoke/case.yaml"),
        CaseRecord(caseId: "DISTRO-001", phase: "11-distro-matrix", harnessType: "distro_fixture", caseYamlRelativePath: "cases/11-distro-matrix/DISTRO-001-alpine-minimal/case.yaml"),
        CaseRecord(caseId: "DISTRO-002", phase: "11-distro-matrix", harnessType: "distro_fixture", caseYamlRelativePath: "cases/11-distro-matrix/DISTRO-002-alpine-package-manager/case.yaml"),
        CaseRecord(caseId: "DISTRO-003", phase: "11-distro-matrix", harnessType: "distro_fixture", caseYamlRelativePath: "cases/11-distro-matrix/DISTRO-003-ubuntu-minimal/case.yaml"),
        CaseRecord(caseId: "DISTRO-004", phase: "11-distro-matrix", harnessType: "distro_fixture", caseYamlRelativePath: "cases/11-distro-matrix/DISTRO-004-ubuntu-package-manager/case.yaml"),
    ]

    func testPhase01_DecodeCases() { runCasesForPhase("01-decode") }
    func testPhase03_SemanticExecCases() { runCasesForPhase("03-semantic-exec") }
    func testPhase04_MMUABICases() { runCasesForPhase("04-mmu-abi") }
    func testPhase05_ELFLoaderCases() { runCasesForPhase("05-elf-loader") }
    func testPhase06_SyscallCases() { runCasesForPhase("06-syscalls-core-fs-net") }
    func testPhase07_ThreadSignalCases() { runCasesForPhase("07-threads-signals-tls") }
    func testPhase08_MuslCases() { runCasesForPhase("08-musl") }
    func testPhase09_GlibcCases() { runCasesForPhase("09-glibc") }
    func testPhase11_DistroMatrixCases() { runCasesForPhase("11-distro-matrix") }

    private func runCasesForPhase(_ phase: String) {
        let caseRecords = Self.allCases
            .filter { $0.phase == phase }
            .sorted {
                if $0.caseId == $1.caseId {
                    return $0.caseYamlRelativePath < $1.caseYamlRelativePath
                }
                return $0.caseId < $1.caseId
            }

        XCTAssertGreaterThan(caseRecords.count, 0, "No explicit cases registered for phase \(phase)")

        for record in caseRecords {
            XCTContext.runActivity(named: "\(record.caseId) [\(record.harnessType)]") { _ in
                runCase(record)
            }
        }
    }

    private func runCase(_ record: CaseRecord) {
        let artifactDir = NSTemporaryDirectory().appending("ixland_cases/\(record.caseId)/")
        try? FileManager.default.removeItem(atPath: artifactDir)
        try? FileManager.default.createDirectory(atPath: artifactDir, withIntermediateDirectories: true)

        guard let resourcesDir = Bundle(for: type(of: self)).resourcePath else {
            XCTFail("No resource path found")
            return
        }

        let caseYaml = (resourcesDir as NSString).appendingPathComponent(record.caseYamlRelativePath)
        if record.harnessType != "decode_golden", !FileManager.default.fileExists(atPath: caseYaml) {
            XCTFail("Missing case.yaml for \(record.caseId) at \(caseYaml)")
            return
        }

        let result: Int32
        switch record.harnessType {
        case "decode_golden":
            result = run_decode_golden_case(record.caseId, artifactDir)
        case "semantic_micro":
            result = run_semantic_micro(caseYaml, artifactDir)
        case "abi_fixture":
            result = run_abi_fixture(caseYaml, artifactDir)
        case "runtime_trace":
            result = run_runtime_trace(caseYaml, artifactDir)
        case "syscall_fixture":
            result = run_syscall_fixture(caseYaml, artifactDir)
        case "elf_fixture":
            result = run_elf_loader(caseYaml, artifactDir)
        case "distro_fixture":
            result = run_distro_fixture(caseYaml, artifactDir)
        case "thread_fixture", "signal_fixture":
            result = run_thread_signal_fixture(caseYaml, artifactDir)
        default:
            XCTFail("Unknown harness type: \(record.harnessType) for case \(record.caseId)")
            return
        }

        XCTAssertEqual(result, 0, "Case \(record.caseId) (\(record.harnessType)) failed with exit code \(result)")
    }
}
