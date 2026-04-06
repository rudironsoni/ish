// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "IXLandInstrumentation",
    platforms: [.iOS(.v14)],
    products: [
        .library(
            name: "IXLandInstrumentation",
            targets: ["IXLandInstrumentation"]
        ),
        .library(
            name: "IXLandInstrumentationBridge",
            targets: ["IXLandInstrumentationBridge"]
        ),
        .library(
            name: "IXLandInstrumentationTracing",
            targets: ["IXLandInstrumentationTracing"]
        ),
    ],
    targets: [
        .target(
            name: "IXLandInstrumentation",
            dependencies: [],
            path: "Sources/IXLandInstrumentation",
            publicHeadersPath: "include"
        ),
        .target(
            name: "IXLandInstrumentationBridge",
            dependencies: ["IXLandInstrumentation"],
            path: "Sources/IXLandInstrumentationBridge",
            publicHeadersPath: ".",
            linkerSettings: [
                .linkedFramework("Foundation"),
            ]
        ),
        .target(
            name: "IXLandInstrumentationTracing",
            dependencies: ["IXLandInstrumentation"],
            path: "Sources/IXLandInstrumentationTracing",
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include/IXLandInstrumentationTracing"),
            ]
        ),
    ]
)
