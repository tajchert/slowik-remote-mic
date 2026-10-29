# Slowik 🐦

Turn an Android phone into a wireless stage microphone for your Mac. Slowik (Polish for *nightingale*) streams
48 kHz mono audio over your local Wi-Fi with ~40 ms latency and exposes it as a system microphone called
**Slowik Mic** — selectable in Google Meet, Zoom, Teams and OBS.

> Status: early MVP, under active development.

## How it works
1. **Android app** captures the mic (no call processing), sends 5 ms UDP packets.
2. **Slowik Receiver** (macOS menu-bar app) buffers, corrects clock drift, applies a voice chain
   (high-pass, compressor, limiter) and plays into the **Slowik Mic** virtual device.
3. Pick **Slowik Mic** as the microphone in your meeting app.

See `docs/plan.md` for the full design and `docs/PROTOCOL.md` for the wire format.

## Building
- Android: see `android/README.md`
- macOS driver: see `macos/Driver/README.md`
- macOS receiver: see `macos/Receiver/README.md`

## Testing without hardware
```
python3 tools/slowik_sim.py receive              # fake Mac
python3 tools/slowik_sim.py send --host <mac-ip> # fake phone (sine tone)
```

## Licence
MIT
