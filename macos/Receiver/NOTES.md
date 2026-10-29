# Handover notes — Mac receiver (branch `mac-receiver`)

## Done
- **Engine** (C++17, C API): PCM16/PCM24 parse + validation, stream_id-keyed slots (v1 plays the first stream and
  drains the others), SPSC packet queue, 640 ms frame ring, redundancy dedupe, seq-window duplicate detection,
  RTP-style seq loss, reorder handling, sender-restart and gap resync, crossfaded mirror-repeat concealment,
  stall-on-underrun, 150 ms link-lost fade + resync, adaptive target (p99 jitter), 16-tap windowed-sinc resampler with
  PI drift control, mute fade, stats snapshot, CONTROL builder.
- **Tests** (`swift test`, 13 tests, about 1 s): protocol byte layout and invalid packets, in-order, PCM24,
  2 % and 5 % loss, reorder + duplicates, jitter burst (target grows then shrinks), ±150 ppm over 10 simulated
  minutes, sender restart, seq wrap, outage → link lost → recovery, operator mute.
- **SlowikCore**: BSD-socket UDP thread (0.0.0.0:47800), CONTROL at 5 Hz to the last audio source, Bonjour
  `_slowik._udp.` named "Slowik on <host>", AVAudioEngine chain, device lookup by `SlowikMic_UID`, 128-frame
  buffer, device-list and configuration-change re-attach, idle host-clock renderer when no device plays.
- **slowik-rx** CLI and `scripts/integration.sh` + `scripts/wavcheck.py`.
- **App**: MenuBarExtra (window style, LSUIElement) with status, battery/RSSI, LIVE/MUTE, trim, pre/post meters,
  loss/buffer/latency/jitter/drift, output picker (Slowik Mic / default monitor / off) with the "driver not
  installed" message, DSP bypass, IPs + QR code. The menu-bar icon turns red when LIVE and orange on link lost.

## Verified how (2026-10-29)
- `swift test`: 13/13 pass.
- `xcodebuild … build`: BUILD SUCCEEDED. The app was launched with `open`; the process was running, it bound
  UDP *:47800, `dns-sd -B _slowik._udp.` listed "Slowik on …", the simulator received CONTROL with jb/latency
  filled in, and the app quit cleanly.
- `scripts/integration.sh 30` (2 % loss, 8 ms jitter, +150 ppm): loss before redundancy 1.85 %, after 0.033 %
  (2 frames), 0 underruns, target ≈ 15 ms, latency estimate ≈ 27 ms, drift −157 ppm at 30 s. WAV: 439.95 Hz,
  −13.47 dBFS RMS (exact), 0 discontinuities, 0 silent runs.
- `slowik-rx --play default` played through the DSP chain at an inaudible level ("Playing to MacBook Pro Speakers").
  `--play slowik` without the driver reports "Slowik Mic driver not installed".

## Known gaps
- **Not tested against the real Slowik Mic driver** (built in parallel). The device-attach code path is shared with
  the default-output path, which is tested.
- Monitor and Slowik Mic can't play at the same time: the engine has a single consumer. A headphone monitor alongside
  the virtual mic needs a second tap/ring (for example, a tap on the main mixer feeding a second AVAudioEngine).
- With `--play`, the CLI writes the WAV from a main-mixer tap at the hardware rate, but the header always says
  48 kHz. This is fine for Slowik Mic (48 kHz) and wrong for a 44.1 kHz default output.
- Latency is an estimate: 5 ms framing + 1 ms network (assumed) + mean jitter + buffer + resampler + device latency.
  It has not been measured end to end.
- For 10 s after every resync the resampler may run at up to ±2000 ppm (3.5 cents) to settle the buffer quickly.
  You can see this in the WAV residual analysis, but it shouldn't be audible.
- No raw or processed backup recording in the app yet (plan item "Backups"). No packet authentication (v2).
- Settings such as trim and output target are not persisted between launches.
- `AudioOutput`'s config-change handler restarts the whole engine, which causes a short gap. That is acceptable for
  device changes.

## Tuning knobs
- `SlowikConfig` (`slowik_config_default` in engine.cpp): `min/max_target_ms` 10/80, `initial_target_ms` 20,
  `margin_ms` 3, `max_ppm` 200, `link_lost_ms` 150, `kp` 0.1, `ki` 0.0025.
- engine.cpp constants: `kConcealTau` 15 ms, `kXfade` 96 samples, `kJitterWindow` 2000 frames (10 s),
  `kRawHold` 10 s, shrink rates 100/250 ppm (quiet threshold −40 dBFS), underrun bump +2 ms, catch-up 10 s at
  5×Kp/±2000 ppm, restart threshold 200 ms back.
- DSP: `AudioOutput.configureDynamics()` and the high-pass settings in `init`. Trim defaults to +12 dB.
