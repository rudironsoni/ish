// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "IXLand",
    platforms: [.iOS(.v14)],
    products: [
        .library(
            name: "IXLandLinuxRuntime",
            type: .dynamic,
            targets: ["IXLandLinuxRuntime"]
        ),
        .executable(
            name: "IXLandTerminal",
            targets: ["IXLandTerminal"]
        ),
    ],
    dependencies: [
        .package(url: "https://github.com/matteo-pacini/libarchive-for-swift", from: "1.0.0"),
        .package(path: "Packages/IXLandInstrumentation"),
    ],
    targets: [
        .target(
            name: "IXLandLinuxRuntime",
            dependencies: [
                .product(name: "libarchive", package: "libarchive-for-swift"),
                .product(name: "IXLandInstrumentation", package: "IXLandInstrumentation"),
                .product(name: "IXLandInstrumentationTracing", package: "IXLandInstrumentation"),
            ],
            path: "Sources/IXLandLinuxRuntime",
            exclude: [
                "include/IXLandLinuxRuntime.h"
            ],
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("../"),
                .define("IXLAND_LINUX_RUNTIME"),
                .define("ISH_PLATFORM_IOS"),
            ],
            linkerSettings: [
                .linkedFramework("Foundation"),
            ]
        ),
        .executableTarget(
            name: "IXLandTerminal",
            dependencies: [
                "IXLandLinuxRuntime",
                .product(name: "IXLandInstrumentation", package: "IXLandInstrumentation"),
                .product(name: "IXLandInstrumentationBridge", package: "IXLandInstrumentation"),
            ],
            path: "Sources/IXLandTerminal",
            exclude: [
                "Tests/",
                "UITests/",
                "targets/",
                "AppLib.xcconfig",
                "CLI.xcconfig",
                "iOS.xcconfig",
                "iSH.xcconfig",
                "Project.xcconfig",
                "StaticLib.xcconfig",
                "XcodeDebug.xcconfig",
                "XcodeDefault.xcconfig",
                "XcodeRelease.xcconfig",
                "xcode-ninja.sh"
            ],
            cSettings: [
                .headerSearchPath("."),
                .headerSearchPath("../IXLandLinuxRuntime/include"),
            ],
            linkerSettings: [
                .linkedFramework("Foundation"),
                .linkedFramework("UIKit"),
                .linkedFramework("CoreGraphics"),
            ]
        ),
        .testTarget(
            name: "IXLandLinuxRuntimeUnitTests",
            dependencies: [
                "IXLandLinuxRuntime"
            ],
            path: "Tests/IXLandLinuxRuntimeUnitTests"
        ),
        .testTarget(
            name: "IXLandLinuxRuntimeFunctionalTests",
            dependencies: [
                "IXLandLinuxRuntime"
            ],
            path: "Tests/IXLandLinuxRuntimeFunctionalTests"
        ),
        .testTarget(
            name: "IXLandLinuxRuntimeEnd2EndTests",
            dependencies: [
                "IXLandLinuxRuntime"
            ],
            path: "Tests/IXLandLinuxRuntimeEnd2EndTests"
        ),
        .testTarget(
            name: "IXLandTerminalUnitTests",
            dependencies: [
                "IXLandTerminal"
            ],
            path: "Tests/IXLandTerminalUnitTests"
        ),
        .testTarget(
            name: "IXLandTerminalEnd2EndTests",
            dependencies: [
                "IXLandTerminal"
            ],
            path: "Tests/IXLandTerminalEnd2EndTests"
        ),
    ]
)
