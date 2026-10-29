// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "SlowikReceiver",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "SlowikEngine", targets: ["SlowikEngine"]),
    ],
    targets: [
        // C++17 real-time engine with a plain C API (include/slowik_engine.h).
        .target(name: "SlowikEngine", cxxSettings: [.unsafeFlags(["-O2"])]),
        // Deterministic simulator used by the tests.
        .target(name: "SlowikSim", dependencies: ["SlowikEngine"], cxxSettings: [.unsafeFlags(["-O2"])]),
        .testTarget(name: "SlowikEngineTests", dependencies: ["SlowikEngine", "SlowikSim"]),
    ],
    cxxLanguageStandard: .cxx17
)
