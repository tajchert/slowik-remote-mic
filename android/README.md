# Slowik for Android

Turns an Android 10+ phone into a wireless stage microphone for the Slowik Mac receiver.

## Build and install
```
cd android
echo "sdk.dir=$HOME/Library/Android/sdk" > local.properties
./gradlew :app:installDebug
```
Requires JDK 21, Android SDK 37, NDK 30.0.16248370 and CMake 3.22.1.

## Using it on stage
1. Start the Mac receiver; the phone must be on the same Wi-Fi.
2. Open Slowik, allow Microphone, Notifications and Local network access.
3. Pick the Mac from the list (or type `IP[:port]`, default 47800).
4. Check the pre-flight card (Do Not Disturb, battery, Wi-Fi, battery optimisation) and tap GO LIVE.
5. Screen colour: grey NO RECEIVER, amber READY, red LIVE (operator has you on air), blue MUTED.
6. Mute: press and hold anywhere on the screen. Lock touches: hold LOCK; hold the circle for 1 s to unlock. Stop: hold STOP (or use the notification).

## Tested devices
_None yet: only verified on an emulator. Add device, Android version, granted Oboe config (shown at the bottom of the live screen) and measured latency here._
