# Handover notes (android-sender)

## Done
- Template stripped to `:app`, `:core:designsystem`, new `:core:audio` (Oboe + CMake C++).
- Native engine: Oboe input (LowLatency, Exclusive requested, 48 kHz mono float, Unprocessed if supported else VoicePerformance) -> SPSC ring -> sender thread -> PCM16 AUDIO packets with 2-frame redundancy; CONTROL parsing; auto-reopen on disconnect.
- App: foreground mic service, Wi-Fi low-latency lock + partial wake lock, battery/RSSI feed, NSD + manual receiver, persisted in SharedPreferences, stage screen, hold-to-mute, touch lock, pre-flight card.

## Verified
- `:app:assembleDebug`, `:app:testDebugUnitTest` pass; host C++ protocol test passes.
- Emulator (Pixel_10_Pro_Fold, API 37) against `tools/slowik_sim.py receive`: ~200 packets/s, 0 missing, app showed LIVE (from sim CONTROL live=1) and MUTED after long-press.

## Not verified / gaps
- No real phone: Exclusive/LowLatency (emulator granted Shared, perf None), real latency, Wi-Fi RSSI, NSD discovery, DND/battery checks, battery-opt flow, lock overlay edge cases, doze/screen-off behaviour.
- Android 17 (API 37) blocks LAN traffic without `ACCESS_LOCAL_NETWORK` (send() fails with EPERM). It is declared and requested at start; on grant-less runs the stream silently sends nothing. Consider a visible warning.
- Emulator mic input is silent/synthetic; level meter and packet content were not checked against real speech.
- Foreground-service stop via `adb am startservice` did not obviously release the Wi-Fi lock in `dumpsys wifi`; not investigated (UI/notification stop path unchecked).
- Receiver NSD resolves use deprecated `resolveService`; fine through API 37.
- Muted flag/zero samples verified only by unit/header test, not by the sim on the wire.
- No launcher icon work; template icon is kept. No Hilt/DataStore by design.
