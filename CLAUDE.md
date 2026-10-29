# Slowik — phone as a stage microphone

Android phone captures the speaker's voice and streams it over UDP to a Mac, which exposes it as a
system microphone ("Slowik Mic") for Google Meet / Zoom / Teams / OBS.

## Read first
- `docs/DECISIONS.md` — cross-cutting rulings (names, IDs, scope). Overrides everything else.
- `docs/PROTOCOL.md` — wire format contract between phone and Mac.
- `docs/plan.md` — original research plan (background; long, read only the section you need).
- `docs/LEDGER.md` — progress ledger: what is done, what is open.

## Layout
- `android/` — Android sender app (Kotlin + C++/Oboe). See `android/CLAUDE.md`.
- `macos/Receiver/` — menu-bar receiver app (SwiftUI + C++ engine). See `macos/Receiver/CLAUDE.md`.
- `macos/Driver/` — "Slowik Mic" HAL AudioServerPlugIn (libASPL, C++). See `macos/Driver/CLAUDE.md`.
- `tools/slowik_sim.py` — reference protocol implementation: fake phone (`send`) and fake Mac (`receive`).

## Conventions
- Repo is public: never commit keystores, signing team IDs, tokens or local paths.
- Commit small and often; messages in imperative mood. Update `docs/LEDGER.md` when finishing a task.
- Build/test narrowly — use the per-module commands in each area's CLAUDE.md.
