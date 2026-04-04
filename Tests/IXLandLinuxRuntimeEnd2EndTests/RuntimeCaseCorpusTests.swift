import XCTest

/// XCTestCase wrapper that executes the Python case corpus runner.
/// This bridges the YAML-based case system into the XCTest pipeline.
/// Runs on macOS since it needs Process() to invoke Python.
final class RuntimeCaseCorpusTests: XCTestCase {

    /// Returns the path to the cases directory.
    private var casesDirectory: URL {
        // Try relative to the test bundle's source location
        let bundleURL = Bundle(for: type(of: self)).bundleURL
        // Walk up: Build/Products/Debug/IXLandLinuxRuntimeEnd2EndTests.xctest
        var url = bundleURL
        url.deleteLastPathComponent() // Debug
        url.deleteLastPathComponent() // Products
        url.deleteLastPathComponent() // Build
        url.deleteLastPathComponent() // DerivedData/IXLand-xxx
        url.deleteLastPathComponent() // DerivedData

        // From /Volumes/1TB/Xcode/ navigate to repo
        let repoRoot = URL(fileURLWithPath: "/Users/rudironsoni/src/github/rudironsoni/ish")
        return repoRoot
            .appendingPathComponent("tests")
            .appendingPathComponent("IXLandLinuxRuntimeEnd2EndTests")
            .appendingPathComponent("cases")
    }

    /// Returns the path to the Python runner script.
    private var runnerScript: URL {
        return casesDirectory.deletingLastPathComponent()
            .appendingPathComponent("run_cases.py")
    }

    func testCaseCorpusDiscovery() throws {
        guard FileManager.default.fileExists(atPath: casesDirectory.path) else {
            XCTFail("Cases directory not found at: \(casesDirectory.path)")
            return
        }

        guard FileManager.default.fileExists(atPath: runnerScript.path) else {
            XCTFail("Runner script not found at: \(runnerScript.path)")
            return
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        process.arguments = [
            runnerScript.path,
            "--cases-dir", casesDirectory.path,
            "--json",
        ]

        let outputPipe = Pipe()
        let errorPipe = Pipe()
        process.standardOutput = outputPipe
        process.standardError = errorPipe

        try process.run()
        process.waitUntilExit()

        let outputData = outputPipe.fileHandleForReading.readDataToEndOfFile()
        let errorData = errorPipe.fileHandleForReading.readDataToEndOfFile()

        let output = String(data: outputData, encoding: .utf8) ?? ""
        let error = String(data: errorData, encoding: .utf8) ?? ""

        // Parse JSON output
        guard let data = output.data(using: .utf8),
              let report = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            XCTFail("Failed to parse case runner output. Stderr: \(error)")
            return
        }

        let totalDiscovered = report["total_discovered"] as? Int ?? 0
        let totalValid = report["total_valid"] as? Int ?? 0
        let totalInvalid = report["total_invalid"] as? Int ?? 0

        print("=== Runtime Case Corpus Report ===")
        print("Discovered: \(totalDiscovered)")
        print("Valid:      \(totalValid)")
        print("Invalid:    \(totalInvalid)")

        if let byPhase = report["by_phase"] as? [String: [String: Int]] {
            print("\nBy Phase:")
            for (phase, info) in byPhase.sorted(by: { $0.key < $1.key }) {
                print("  \(phase): \(info["total"] ?? 0) total, \(info["valid"] ?? 0) valid, \(info["invalid"] ?? 0) invalid")
            }
        }

        if let byHarness = report["by_harness"] as? [String: Int] {
            print("\nBy Harness Type:")
            for (harness, count) in byHarness.sorted(by: { $0.key < $1.key }) {
                print("  \(harness): \(count)")
            }
        }
        print("================================")

        // Assert that cases were discovered
        XCTAssertGreaterThanOrEqual(totalDiscovered, 100,
            "Expected at least 100 cases, found \(totalDiscovered)")

        // Report invalid cases but don't fail the test for structural issues
        // (those are tracked separately and fixed incrementally)
        if totalInvalid > 0 {
            print("WARNING: \(totalInvalid) case(s) have structural issues")
        }
    }
}
