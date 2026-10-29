# Cross-cutting decisions (override individual plans)

Every agent reads this. If you need to deviate, add a dated entry here and say why.

1. **Name**: product "Slowik" (Polish for nightingale). Virtual device name **"Slowik Mic"**, manufacturer "Slowik".
2. **Android package / applicationId**: `pl.tajchert.slowik`. Min SDK 29 (Android 10, needed for low-latency Wi-Fi lock), target the template's SDK.
3. **Protocol**: `docs/PROTOCOL.md` is the contract. Port 47800, Bonjour `_slowik._udp.`, 48 kHz mono, 5 ms frames, PCM16, 2 frames/packet.
4. **MVP scope** (plan phase 2): single phone, PCM only, no Opus, no packet auth, no QR scanning on the phone (manual IP + NSD). Mac shows QR/text for humans.
5. **Virtual device (plan option B, loopback)**: HAL AudioServerPlugIn built with libASPL (MIT), C++17, CMake.
   - Device UID `SlowikMic_UID`, model UID `SlowikMic_ModelUID`, name "Slowik Mic".
   - 48 kHz only, 2 channels input + 2 channels output (dual-mono), float32.
   - Whatever is written to the output stream is readable on the input stream (loopback, ring buffer).
   - The receiver app finds the device **by UID** `SlowikMic_UID` and plays into it. Meet/Zoom record from it.
   - No code from BlackHole (GPL). Roc VAD (MPL) may be read as a reference only.
6. **Mac receiver**: SwiftUI menu-bar app (`MenuBarExtra`) + C++ engine (jitter buffer, drift resampler, concealment) in a SwiftPM C++ target with unit tests. DSP chain for MVP = Apple Audio Units in `AVAudioEngine`. Project generated with XcodeGen (`project.yml` committed, `.xcodeproj` gitignored) or plain SwiftPM — the receiver agent decides and documents it.
7. **Code signing**: local development uses ad-hoc / personal signing. No secrets, keystores or team IDs committed (repo is public).
8. **Licence**: MIT for all Slowik code.
9. **Repo layout**: `android/` (Gradle project), `macos/Receiver/`, `macos/Driver/`, `tools/`, `docs/`. Each area has its own `CLAUDE.md`.
