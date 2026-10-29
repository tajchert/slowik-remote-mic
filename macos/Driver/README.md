# Slowik Mic driver

Virtual microphone for macOS (AudioServerPlugIn). The Slowik receiver app plays audio into the device
"Slowik Mic"; Meet/Zoom/Teams/OBS record from it.

## Build / test
    ./build.sh                                  # needs cmake + Xcode CLT; produces build/SlowikMic.driver (ad-hoc signed)
    (cd build && ctest -R loopback_ring --output-on-failure)

## Install / uninstall
    ./install.sh      # sudo copy to /Library/Audio/Plug-Ins/HAL + restart coreaudiod (audio blips for a moment)
    ./uninstall.sh

## Verify
    system_profiler SPAudioDataType | grep -A6 "Slowik Mic"
    tools/loopback_check.sh     # plays 1 kHz tone into the device, records from it, prints level/latency/PASS

The check needs microphone permission for your terminal (System Settings > Privacy & Security > Microphone);
without it the recording is silent and the check fails.

## Troubleshooting
- Device missing: wait ~3 s after install, check `ls /Library/Audio/Plug-Ins/HAL`, and `codesign -v` the installed bundle.
- Restart the Meet tab / Zoom / Teams / OBS after install (they cache the device list), then pick "Slowik Mic".
- Logs: `log stream --predicate 'process == "coreaudiod"'`
- Silence in Meet: the receiver app must be running and playing; with no writer the device deliberately outputs silence.
- Ad-hoc signing works for local use on Apple Silicon HAL plugins; if coreaudiod refuses to load it, check the log above.
