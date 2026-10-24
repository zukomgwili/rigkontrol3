// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "RigKontrol3",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(name: "RigKontrol3Core", targets: ["RigKontrol3Core"]),
        .executable(name: "rk3bridge", targets: ["RK3Bridge"]),
        .executable(name: "rk3controller", targets: ["RK3ControllerApp"]),
        .executable(name: "rk3probe", targets: ["RK3Probe"]),
    ],
    targets: [
        .target(
            name: "RigKontrol3Core",
            dependencies: ["CLibusb"],
            linkerSettings: [
                .linkedFramework("CoreMIDI"),
                .linkedFramework("IOKit"),
            ]
        ),
        .executableTarget(
            name: "RK3Bridge",
            dependencies: ["RigKontrol3Core"]
        ),
        .executableTarget(
            name: "RK3ControllerApp",
            dependencies: ["RigKontrol3Core"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("ServiceManagement"),
            ]
        ),
        .executableTarget(
            name: "RK3Probe",
            dependencies: ["RigKontrol3Core"]
        ),
        .testTarget(
            name: "RigKontrol3CoreTests",
            dependencies: ["RigKontrol3Core"]
        ),
        .systemLibrary(
            name: "CLibusb",
            pkgConfig: "libusb-1.0",
            providers: [
                .brew(["libusb"])
            ]
        ),
    ]
)
