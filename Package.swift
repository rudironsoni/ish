// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "iSH",
    platforms: [
        .iOS(.v14)
    ],
    products: [
        .library(
            name: "iSH",
            targets: ["iSH"]
        )
    ],
    dependencies: [
        .package(url: "https://github.com/matteo-pacini/libarchive-for-swift", from: "1.0.0")
    ],
    targets: [
        .target(
            name: "iSH",
            dependencies: [
                .product(name: "libarchive", package: "libarchive-for-swift")
            ]
        )
    ]
)
