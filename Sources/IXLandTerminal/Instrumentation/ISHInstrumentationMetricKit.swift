//
//  ISHInstrumentationMetricKit.swift
//  iSH
//
//  MetricKit integration for diagnostic metrics.
//  Registers for MetricKit only during Stage 1 (activation).
//  Must NOT touch main.m or do startup proof logic.
//

import Foundation
import MetricKit

/**
 * ISHInstrumentationMetricKit provides MetricKit integration for diagnostics.
 * It does NOT own app bootstrap - it only registers during Stage 1 (activation).
 * This provides access to app launch metrics, CPU metrics, memory metrics, and disk metrics.
 */
@objc
public class ISHInstrumentationMetricKit: NSObject {

    /**
     * Setup the MetricKit integration. Called during bootstrap.
     * Currently a no-op as MetricKit registration happens during activation.
     */
    @objc
    public static func setup() {
        // MetricKit subscriber registration happens during activation (Stage 1)
        // Stub for initial compiling pass
    }

    /**
     * Activate the MetricKit integration. Called during activation (Stage 1).
     * Registers for MetricKit diagnostics.
     */
    @objc
    public static func activate() {
        // Register for MetricKit payload delivery on the main queue
        if #available(iOS 14.0, *) {
            MXMetricManager.shared.add(ISHMetricKitSubscriber.shared)
        }
    }

    /**
     * Deactivate the MetricKit integration.
     * Unregisters from MetricKit diagnostics.
     */
    @objc
    public static func deactivate() {
        if #available(iOS 14.0, *) {
            MXMetricManager.shared.remove(ISHMetricKitSubscriber.shared)
        }
    }
}

/**
 * Private subscriber class that conforms to MXMetricManagerSubscriber.
 * Receives MetricKit payloads containing diagnostic metrics.
 */
@available(iOS 14.0, *)
private class ISHMetricKitSubscriber: NSObject, MXMetricManagerSubscriber {

    static let shared = ISHMetricKitSubscriber()

    /**
     * Called when MetricKit delivers a metrics payload.
     * Contains app launch metrics, CPU metrics, memory metrics, and cellular metrics.
     */
    func didReceive(_ payloads: [MXMetricPayload]) {
        for payload in payloads {
            // Process MetricKit payload
            // This is a stub implementation for initial compiling pass
            // In a full implementation, this would forward metrics to sinks

            if let appLaunchMetrics = payload.applicationLaunchMetrics {
                // Access launch metrics (histogram of launch times)
                _ = appLaunchMetrics.histogrammedTimeToFirstDraw
            }

            if let cpuMetrics = payload.cpuMetrics {
                // Access CPU metrics
                _ = cpuMetrics.cumulativeCPUTime
            }

            if let memoryMetrics = payload.memoryMetrics {
                // Access memory metrics
                _ = memoryMetrics.peakMemoryUsage
            }

            if let diskMetrics = payload.diskIOMetrics {
                // Access disk I/O metrics
                _ = diskMetrics.cumulativeLogicalWrites
            }
        }
    }

    /**
     * Called when MetricKit delivers diagnostic payloads (crashes, CPU exceptions, etc).
     * Available in iOS 14.0+.
     */
    func didReceive(_ payloads: [MXDiagnosticPayload]) {
        for payload in payloads {
            // Process diagnostic payload
            // This is a stub implementation for initial compiling pass

            // Access crash diagnostics
            if let crashDiagnostics = payload.crashDiagnostics {
                for _ in crashDiagnostics {
                    // Process crash diagnostic
                }
            }

            // Access CPU exception diagnostics
            if let cpuExceptionDiagnostics = payload.cpuExceptionDiagnostics {
                for _ in cpuExceptionDiagnostics {
                    // Process CPU exception
                }
            }
        }
    }
}
