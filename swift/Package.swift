// swift-tools-version: 5.10
import PackageDescription

let package = Package(
    name: "DeskPetHardware",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "DeskPetHardware", targets: ["DeskPetHardware"]),
        .executable(name: "deskpet-swift-demo", targets: ["DeskPetHardwareDemo"]),
    ],
    targets: [
        .target(name: "DeskPetHardware"),
        .executableTarget(name: "DeskPetHardwareDemo", dependencies: ["DeskPetHardware"]),
        .testTarget(name: "DeskPetHardwareTests", dependencies: ["DeskPetHardware"]),
    ]
)
