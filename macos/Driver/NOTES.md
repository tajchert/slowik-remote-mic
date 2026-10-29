# Driver handover

## Done
- libASPL v3.1.2 driver, device per DECISIONS item 5, loopback via `LoopbackRing` (16384 frames), clear on Start/StopIO.
- `build.sh`, `install.sh`, `uninstall.sh`, `tools/loopback_check.sh`, docs, ctest host test (8 scenarios: wrap, stale, gap, read-before-write, clear).

## Verified
- `./build.sh` OK (universal arm64+x86_64), `codesign -v` OK, bundle layout + `plutil -lint` OK, entry point symbol exported, ctest green.
- NOT verified: install + live loopback (no passwordless sudo on the build machine). Run `./install.sh` then `tools/loopback_check.sh`.

## Design notes
- Latency: zero safety offset/latency. Writer is ahead of wall clock, reader behind, so data exists at read time; loopback delay ~ one IO buffer.
- Zero timestamp period 512 frames (~10.7 ms) for fine clock estimation.
- Mixing is done by HAL (EnableMixing); multiple writers are summed, so only one app should play into it.

## Known gaps
- Reader running ahead of the writer (underrun) gets silence for those frames; if dropouts appear, add a small read-behind offset in `LoopbackRing::Read` (e.g. 1 IO buffer).
- Option C (shared memory straight from receiver to driver, skipping the HAL output path) is future work.
- Not notarized; ad-hoc signed only.
