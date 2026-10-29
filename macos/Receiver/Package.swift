// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "SlowikReceiver",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "SlowikEngine", targets: ["SlowikEngine"]),
        .library(name: "SlowikCore", targets: ["SlowikCore"]),
        .executable(name: "slowik-rx", targets: ["slowik-rx"]),
    ],
    targets: [
        // C++17 real-time engine with a plain C API (include/slowik_engine.h). Always optimised: the
        // render path and the long simulated test runs should not depend on the build configuration.
        .target(name: "SlowikEngine", cxxSettings: [.unsafeFlags(["-O2"])]),
        // Deterministic network/clock simulator used by the tests.
        .target(name: "SlowikSim", dependencies: ["SlowikEngine"], cxxSettings: [.unsafeFlags(["-O2"])]),
        // Swift glue shared by the app and the CLI: UDP socket thread, CONTROL, Bonjour, AVAudioEngine output.
        .target(name: "SlowikCore", dependencies: ["SlowikEngine"]),
        // Headless receiver / integration-test harness.
        .executableTarget(name: "slowik-rx", dependencies: ["SlowikCore", "SlowikEngine"]),
        .testTarget(name: "SlowikEngineTests", dependencies: ["SlowikEngine", "SlowikSim"]),
    ],
    cxxLanguageStandard: .cxx17
)
