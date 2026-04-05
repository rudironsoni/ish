/*
 * Harness Runner API - Runtime Functional Harnesses
 *
 * Declares the run_* entry points for runtime functional harness types.
 * Each function accepts a case.yaml path and artifact directory,
 * executes the appropriate test, writes artifacts, and returns
 * 0 on success, non-zero on failure.
 *
 * These functions are callable from both main() (CLI mode) and
 * XCTestCase wrappers (test mode).
 *
 * NOTE: iOS app orchestration harnesses (APPSIM, APP cases) are NOT
 * included here - they belong in IXLandTerminalEnd2EndTests and run
 * on the host side, not inside iOS test bundles.
 */

#ifndef HARNESS_RUNNER_H
#define HARNESS_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Decode golden harness - validates decoder against golden references */
int run_decode_golden(const char *case_yaml, const char *artifact_dir);

/* Generator golden harness - validates TCTI gadget emission */
int run_generator_golden(const char *case_yaml, const char *artifact_dir);

/* Semantic micro harness - validates semantic execution through TCTI */
int run_semantic_micro(const char *case_yaml, const char *artifact_dir);

/* ABI fixture harness - validates Linux AArch64 ABI and MMU */
int run_abi_fixture(const char *case_yaml, const char *artifact_dir);

/* Runtime trace harness - validates trace ring buffer and boundary events */
int run_runtime_trace(const char *case_yaml, const char *artifact_dir);

/* Syscall fixture harness - validates syscall implementations */
int run_syscall_fixture(const char *case_yaml, const char *artifact_dir);

/* ELF loader harness - validates ELF loading */
int run_elf_loader(const char *case_yaml, const char *artifact_dir);

/* Distro fixture harness - validates musl/glibc distro compatibility */
int run_distro_fixture(const char *case_yaml, const char *artifact_dir);

/* Thread/signal fixture harness - validates threading and signals */
int run_thread_signal_fixture(const char *case_yaml, const char *artifact_dir);

#ifdef __cplusplus
}
#endif

#endif /* HARNESS_RUNNER_H */
