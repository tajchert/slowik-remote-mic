# macos/Driver — "Slowik Mic" virtual microphone (HAL AudioServerPlugIn)

libASPL (MIT, v3.1.2 via CMake FetchContent) based driver. It only copies samples: whatever a client writes to the
output stream is readable on the input stream. No networking, no DSP. The Receiver app plays into it.

## Device contract (docs/DECISIONS.md item 5 — do not change)
- Name "Slowik Mic", manufacturer "Slowik", device UID `SlowikMic_UID`, model UID `SlowikMic_ModelUID`.
- 48000 Hz only, float32 interleaved, 2-channel output stream + 2-channel input stream. Receiver finds it by UID.
- Safety offset 0, device/stream latency 0, zero-timestamp period 512 frames.

## Layout
- `src/LoopbackRing.hpp` — lock-free ring indexed by sample time, no CoreAudio (timing/staleness reasoning in the comment).
- `src/Driver.cpp` — libASPL device, handler (`OnWriteMixedOutput` -> ring, `OnReadClientInput` <- ring, clear on Start/StopIO), entry point `SlowikMicEntryPoint`.
- `Info.plist.in` — CFPlugInFactories/Types (factory UUID 9C3DEE6A-..., type 443ABAB8-...).
- `test/LoopbackRingTest.cpp` — host test. `tools/loopback_check.{cpp,sh}` — end-to-end check against the installed driver.

## Commands (run in macos/Driver)
- Build + ad-hoc sign: `./build.sh` -> `build/SlowikMic.driver`
- Test: `cd build && ctest -R loopback_ring --output-on-failure` (do NOT run bare `ctest`: it also lists libASPL's own gtest suite, which is not built)
- Install/uninstall (sudo): `./install.sh` / `./uninstall.sh`
- Verify installed: `tools/loopback_check.sh`
- Build only builds targets `SlowikMic` and `loopback_ring_test` (libASPL's gtest target fails to link for universal binaries).
