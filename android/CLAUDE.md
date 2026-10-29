# Slowik Android sender

Phone side of Slowik (wireless stage mic). Wire format: `../docs/PROTOCOL.md`; decisions: `../docs/DECISIONS.md`.
Package `pl.tajchert.slowik`, minSdk 29, compile/target SDK 37 (needs `ACCESS_LOCAL_NETWORK`, see NOTES.md).

## Layout
- `app/` Compose UI + `StreamingService` (foreground, microphone). `stream/` = service, `StreamController` (process-wide StateFlows), receiver parsing/store, `StageState` rules. `net/NsdDiscovery`. `Preflight.kt`. `ui/` stateless screens. `SlowikViewModel` exposes one `UiState`.
- `core/audio/` Android library + C++ (CMake, Oboe via prefab). `slowik_protocol.h` = pure packet code; `engine.cpp` = Oboe callback -> SPSC ring -> sender thread -> UDP; `jni.cpp`; Kotlin `SlowikAudioEngine`.
- `core/designsystem/` theme only (`SlowikTheme`).
- No DI framework: plain constructors.

## Commands (run from `android/`)
- Build: `./gradlew :app:assembleDebug`
- JVM tests: `./gradlew :app:testDebugUnitTest`
- Host C++ protocol tests: `core/audio/src/test/cpp/run_host_tests.sh`
- Install: `./gradlew :app:installDebug`
- Debug auto-start (debug builds only): `adb shell am start -n pl.tajchert.slowik/.MainActivity --es autostart 10.0.2.2:47800`
- Fake Mac: `python3 ../tools/slowik_sim.py receive`

## Conventions
- Audio callback: no allocation/locks/logging/syscalls. Everything else lives on the sender thread.
- Packet layout changes go to PROTOCOL.md and `slowik_protocol.h` + its host test together.
- `local.properties` is gitignored (needs `sdk.dir`). Never commit keystores or tokens.
