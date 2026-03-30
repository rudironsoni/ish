//
//  ISHInstrumentationOpenTelemetry.swift
//  iSH
//
//  Minimal bridge to OpenTelemetry.
//  This is a stub implementation for initial compiling pass.
//  Activates during Stage 1 (after bootstrap).
//

import Foundation

/**
 * ISHInstrumentationOpenTelemetry is a minimal bridge to OpenTelemetry.
 * It does NOT own app bootstrap - it only activates during Stage 1.
 * This is currently a no-op stub that will be expanded with real OTel integration.
 */
@objc
public class ISHInstrumentationOpenTelemetry: NSObject {

    /**
     * Setup the OpenTelemetry bridge. Called during bootstrap.
     * Currently a no-op stub.
     */
    @objc
    public static func setup() {
        // TODO: Initialize OpenTelemetry SDK when integrating real OTel
        // Stub for initial compiling pass
    }

    /**
     * Activate the OpenTelemetry bridge. Called during activation (Stage 1).
     * Currently a no-op stub.
     */
    @objc
    public static func activate() {
        // TODO: Activate OpenTelemetry exporters when integrating real OTel
        // Stub for initial compiling pass
    }

    /**
     * Record a semantic event to OpenTelemetry.
     * Currently a no-op stub.
     * Takes an Int32 which maps to the ISHInstrumentationEvent enum.
     */
    @objc
    public static func recordEvent(_ event: Int32) {
        // TODO: Record event to OTel when integrating real OpenTelemetry
        // Stub for initial compiling pass
    }

    /**
     * Begin a timed interval for OpenTelemetry tracing.
     * Currently a no-op stub.
     */
    @objc
    public static func beginInterval(_ name: String, attributes: [String: Any]?) {
        // TODO: Begin OTel span when integrating real OpenTelemetry
        // Stub for initial compiling pass
    }

    /**
     * End a timed interval for OpenTelemetry tracing.
     * Currently a no-op stub.
     */
    @objc
    public static func endInterval(_ name: String, attributes: [String: Any]?) {
        // TODO: End OTel span when integrating real OpenTelemetry
        // Stub for initial compiling pass
    }
}
