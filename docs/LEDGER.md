# Progress ledger

## Done
- Foundation: protocol v1 spec, decisions, simulator (`tools/slowik_sim.py`), repo layout.
- Mac driver `macos/Driver/` (merged from `mac-driver`): libASPL loopback "Slowik Mic", host tests. Not yet installed/verified in coreaudiod.
- Android sender `android/` (merged from `android-sender`): Oboe capture, UDP sender, stage UI, pre-flight (incl. local network permission).
- Mac receiver `macos/Receiver/` (merged from `mac-receiver`): C++ engine (jitter buffer, PI drift resampler), menu-bar app, `slowik-rx` CLI.

## Real-device E2E (2026-10-29, OnePlus CPH2747, Android API 37, same home Wi-Fi)
- Oboe granted: 48 kHz, LowLatency (perf=12), Unprocessed (preset 9), **Shared** (not Exclusive), burst 240.
- Phone → `slowik-rx`: 200 pkt/s, 0 loss, jitter 3–4 ms, battery/RSSI received.
- Fixed: start-up burst (~300 ms) anchored the jitter buffer; now re-anchors + head-drops, settles to target in <2 s (4685577).
- Driver installed; loopback check PASS (~8 ms loopback).
- Fixed (d20608a): head-drop in AVAudioEngine render path, drift estimate on real data, stream locked to one sender. Real phone → Slowik Mic: 38–43 ms, 0 % loss.
- README with real screenshots, diagrams and charts (`docs/images/`, sources in `docs/images/src/`).

## Open
- Install driver (needs sudo) and verify Slowik Mic in Google Meet.
- Measure real end-to-end latency (clap test).
- QR scan on phone, packet auth, Opus fallback, shared-memory driver (option C), multi-phone, iOS.
