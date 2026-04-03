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
        .package(url: "https://github.com/matteo-pacini/libarchive-for-swift", from: "1.0.0")
    ],
    targets: [
        .target(
            name: "IXLandLinuxRuntime",
            dependencies: [
                .product(name: "libarchive", package: "libarchive-for-swift")
            ],
            path: "Sources/IXLandLinuxRuntime",
            exclude: [
                "include/IXLandLinuxRuntime.h"
            ],
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("."),
                .headerSearchPath("emu"),
                .headerSearchPath("emu/aarch64"),
                .headerSearchPath("kernel"),
                .headerSearchPath("kernel/aarch64"),
                .headerSearchPath("fs"),
                .headerSearchPath("fs/proc"),
                .headerSearchPath("platform"),
                .headerSearchPath("platform/ios"),
                .headerSearchPath("tcti"),
                .headerSearchPath("tcti/aarch64"),
                .headerSearchPath("util"),
                .unsafeFlags(["-x", "assembler-with-cpp"]),
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
                "IXLandLinuxRuntime"
            ],
            path: "Sources/IXLandTerminal",
            exclude: [
                "Tests/",
                "UITests/",
                "targets/",
                "*.xcconfig",
                "xcode-ninja.sh",
                "gen_apk_repositories.py"
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
