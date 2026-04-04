/*
 * IXLandLinuxRuntimeFunctionalTests
 *
 * XCTestCase wrapper that executes the 131 YAML case corpus through
 * the harness C run_* functions. Each case is discovered from the
 * bundle resources and routed to the correct harness by harness_type.
 */

import XCTest

// MARK: - Harness Runner C API

@_silgen_name("run_decode_golden")
private func run_decode_golden(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_generator_golden")
private func run_generator_golden(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_semantic_micro")
private func run_semantic_micro(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_abi_fixture")
private func run_abi_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_runtime_trace")
private func run_runtime_trace(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_ios_fixture")
private func run_ios_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_syscall_fixture")
private func run_syscall_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_elf_loader")
private func run_elf_loader(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_distro_fixture")
private func run_distro_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

@_silgen_name("run_thread_signal_fixture")
private func run_thread_signal_fixture(_ case_yaml: UnsafePointer<CChar>, _ artifact_dir: UnsafePointer<CChar>) -> Int32

// MARK: - Case Record

struct CaseRecord {
    let caseId: String
    let phase: String
    let harnessType: String
    let caseYamlPath: String
}

// MARK: - Test Class

final class IXLandLinuxRuntimeFunctionalTests: XCTestCase {

    private func extractYamlField(_ content: String, _ field: String) -> String? {
        let prefix = "\(field):"
        for line in content.components(separatedBy: "\n") {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.hasPrefix(prefix) {
                let value = String(trimmed.dropFirst(prefix.count)).trimmingCharacters(in: .whitespaces)
                if value.hasPrefix("\"") && value.hasSuffix("\"") {
                    return String(value.dropFirst().dropLast())
                }
                return value
            }
        }
        return nil
    }
    
    /// Extract case ID from YAML (field is "id" not "case_id")
    private func extractCaseId(_ content: String) -> String? {
        return extractYamlField(content, "id")
    }
    
    /// Extract harness type from YAML (field is "harness" not "harness_type")
    private func extractHarnessType(_ content: String) -> String? {
        return extractYamlField(content, "harness")
    }

    private func runCase(_ record: CaseRecord) {
        let artifactDir = NSTemporaryDirectory().appending("ixland_cases/\(record.caseId)/")
        try? FileManager.default.removeItem(atPath: artifactDir)
        try? FileManager.default.createDirectory(atPath: artifactDir, withIntermediateDirectories: true)

        let caseYaml = record.caseYamlPath
        let result: Int32

        switch record.harnessType {
        case "decode_golden":
            result = run_decode_golden(caseYaml, artifactDir)
        case "generator_golden":
            result = run_generator_golden(caseYaml, artifactDir)
        case "semantic_micro":
            result = run_semantic_micro(caseYaml, artifactDir)
        case "abi_fixture":
            result = run_abi_fixture(caseYaml, artifactDir)
        case "runtime_trace":
            result = run_runtime_trace(caseYaml, artifactDir)
        case "ios_fixture":
            result = run_ios_fixture(caseYaml, artifactDir)
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

    // MARK: - Tests by Phase

    func testPhase01_DecodeCases() {
        runCasesForPhase("01-decode")
    }

    func testPhase02_GeneratorCases() {
        runCasesForPhase("02-generator")
    }

    func testPhase03_SemanticExecCases() {
        runCasesForPhase("03-semantic-exec")
    }

    func testPhase04_MMUABICases() {
        runCasesForPhase("04-mmu-abi")
    }

    func testPhase05_ELFLoaderCases() {
        runCasesForPhase("05-elf-loader")
    }

    func testPhase06_SyscallCases() {
        runCasesForPhase("06-syscalls-core-fs-net")
    }

    func testPhase07_ThreadSignalCases() {
        runCasesForPhase("07-threads-signals-tls")
    }

    func testPhase08_MuslCases() {
        runCasesForPhase("08-musl")
    }

    func testPhase09_GlibcCases() {
        runCasesForPhase("09-glibc")
    }

    func testPhase10_iOSToolingCases() {
        runCasesForPhase("10-tooling-stability-ios")
    }

    func testPhase11_DistroMatrixCases() {
        runCasesForPhase("11-distro-matrix")
    }

    private func runCasesForPhase(_ phase: String) {
        let bundle = Bundle(for: type(of: self))
        guard let resourcesDir = bundle.resourcePath else {
            XCTFail("No resource path found")
            return
        }

        let fm = FileManager.default
        let casesDir = (resourcesDir as NSString).appendingPathComponent("cases/\(phase)")

        guard fm.fileExists(atPath: casesDir) else {
            print("[SKIP] Phase \(phase) not found in resources")
            return
        }

        var caseRecords: [CaseRecord] = []

        func walk(_ dir: String) {
            guard let entries = try? fm.contentsOfDirectory(atPath: dir) else { return }
            for entry in entries {
                let fullPath = (dir as NSString).appendingPathComponent(entry)
                var isDir: ObjCBool = false
                if fm.fileExists(atPath: fullPath, isDirectory: &isDir) {
                    if isDir.boolValue {
                        walk(fullPath)
                    } else if entry == "case.yaml" {
                        if let content = try? String(contentsOfFile: fullPath, encoding: .utf8) {
                            let caseId = extractCaseId(content) ?? "UNKNOWN"
                            let harnessType = extractHarnessType(content) ?? "unknown"
                            caseRecords.append(CaseRecord(
                                caseId: caseId,
                                phase: phase,
                                harnessType: harnessType,
                                caseYamlPath: fullPath
                            ))
                        }
                    }
                }
            }
        }

        walk(casesDir)

        print("[\(phase)] Found \(caseRecords.count) cases")
        
        // Debug: print first few cases found
        if !caseRecords.isEmpty {
            for record in caseRecords.prefix(3) {
                print("  - \(record.caseId) [\(record.harnessType)]")
            }
        }
        
        XCTAssertGreaterThan(caseRecords.count, 0, "No cases found for phase \(phase)")

        for record in caseRecords.sorted(by: { $0.caseId < $1.caseId }) {
            print("  Executing \(record.caseId)...")
            runCase(record)
        }
    }
}
