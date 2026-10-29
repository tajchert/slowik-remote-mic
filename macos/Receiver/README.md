# Slowik Receiver (macOS)

Menu-bar app that turns the Slowik phone stream into a microphone for Google Meet, Zoom, Teams or OBS.
It receives 48 kHz audio over Wi-Fi, smooths out network jitter, corrects the phone/Mac clock drift, applies a
broadcast-style voice chain and plays the result into the **Slowik Mic** virtual device.

## Build

Requirements: Xcode 16+ (tested with Xcode 27), [XcodeGen](https://github.com/yonaskolb/XcodeGen) (`brew install xcodegen`).

```sh
cd macos/Receiver
swift test                       # engine unit tests (about a second)
cd App && xcodegen generate
xcodebuild -project SlowikReceiverApp.xcodeproj -scheme SlowikReceiver -configuration Release \
  -derivedDataPath build/DerivedData build
open "build/DerivedData/Build/Products/Release/Slowik Receiver.app"
```

Or open `App/SlowikReceiverApp.xcodeproj` in Xcode and press Run. The app is signed ad-hoc ("Sign to Run Locally");
no Apple developer account is needed. On first launch macOS asks for **Local Network** access — allow it, otherwise
the phone cannot reach the Mac.

The app is **not sandboxed** for now. It needs to bind a UDP port, advertise Bonjour and pick a specific CoreAudio
output device (the Slowik Mic loopback driver) by UID. These work under the sandbox with the right entitlements, but
that only matters for Mac App Store distribution, and the driver can't ship there anyway. The app will be revisited
when it gets Developer ID signing and notarisation.

The virtual device itself is built separately in `macos/Driver/`. Without it the app still runs and shows
**"Slowik Mic driver not installed"**. You can then use *Monitor on default output* to hear the phone on the Mac's
speakers or headphones.

## Headless receiver (`slowik-rx`)

```sh
swift build -c release --product slowik-rx
BIN=$(swift build -c release --show-bin-path)/slowik-rx
$BIN --wav out.wav --seconds 60          # receive, print stats every second, write the WAV
$BIN --play slowik --live                # play to Slowik Mic through the DSP chain, tell the phone it's LIVE
$BIN --play default --trim 6             # monitor on the default output
```

`scripts/integration.sh 30` runs `slowik-rx` against the protocol simulator (`tools/slowik_sim.py`) with 2 % loss,
8 ms jitter and 150 ppm clock skew, then analyses the WAV.

## Operator guide

1. Start **Slowik Receiver**. A microphone icon appears in the menu bar. Click it to open the panel.
2. On the phone, open Slowik and pick **"Slowik on <your Mac>"**, or type the address shown in the panel
   (`slowik://<ip>:47800`; the QR code holds the same text).
3. The panel shows **Connected**, the phone's battery and Wi-Fi signal, and moving **In** and **Out** meters.
4. Speak at the distance you will use on stage. Set **Trim** so normal speech peaks around −12 to −6 dBFS on the
   **Out** meter (the default is +12 dB).
5. In Google Meet, go to *Settings → Audio → Microphone* and choose **Slowik Mic**. Set noise cancellation to
   **Off or Low**, and turn off any "adjust volume automatically" option if offered. In Zoom, choose Slowik Mic,
   set background-noise suppression to **Low**, and enable *Original sound for musicians* if you like. Restart the
   meeting app or reload the Meet tab after installing the driver, so the device shows up.
6. When you go on air, press **Go LIVE**. The button turns red, the menu-bar icon turns red, and the phone shows
   LIVE. **Mute** silences the mic with a short fade and shows MUTED on the phone.

Readouts to watch:

- **Loss** should stay under 1 %. This counts audio that was really lost, after the built-in redundancy.
- **Latency** should be under 50 ms.
- **Buffer** shows the current fill and the target in ms. It grows by itself on a bad network and shrinks slowly.
- **Drift** is usually tens of ppm. It is corrected continuously.
- **Link lost** means no audio for 150 ms. The output fades to silence and recovers automatically when packets
  return.
- **Bypass voice processing** turns off the high-pass, compressor and limiter, which is useful for A/B checks. The
  trim stays on.

## Voice chain (AVAudioEngine, MVP)

| Stage | Setting |
|---|---|
| Trim | AVAudioUnitEQ global gain, −24 to +24 dB, default +12 dB |
| High-pass | two cascaded 12 dB/oct sections at 90 Hz (24 dB/oct) |
| Dynamics (Apple DynamicsProcessor) | 2:1 expander below −50 dBFS; compression from −24 dBFS, headroom 8 dB, attack 7 ms, release 150 ms, +4 dB make-up |
| Peak limiter (Apple PeakLimiter) | attack 2 ms, decay 50 ms, followed by −1 dB output gain (ceiling −1 dBFS) |

Output is dual-mono (the same signal on both channels) at 48 kHz, into the 2-channel Slowik Mic device. The app
requests a 128-frame IO buffer.

## Licence

MIT
