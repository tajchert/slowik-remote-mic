# Slowik wire protocol v1

The single source of truth for phone ↔ Mac communication. Both sides MUST match this byte for byte.
`tools/slowik_sim.py` implements both directions and is the reference for tests.

## Transport

- UDP. The Mac receiver binds `0.0.0.0:47800` (default, configurable).
- The phone sends audio packets from one UDP socket to `<mac-ip>:47800`. The Mac sends control packets back to the
  source address/port of the most recent audio packet of that stream.
- Discovery: the Mac advertises Bonjour/DNS-SD service type `_slowik._udp.` on its port, instance name
  `Slowik on <hostname>`. Android resolves it with NSD. Manual `ip:port` entry is always available as a fallback.
  The Mac UI also shows `slowik://<ip>:<port>` as text + QR code.
- All multi-byte integers are **little-endian**. All packets start with the 4 ASCII bytes `SLWK`.

## Audio format (fixed in v1)

- 48 000 Hz, mono.
- One **frame** = 240 samples = 5 ms.
- Codec `1` = PCM signed 16-bit LE (480 bytes/frame). Codec `2` = PCM signed 24-bit LE packed (720 bytes/frame).
  v1 senders send codec 1. Receivers MUST accept both.

## Packet type 1: AUDIO (phone → Mac), 200 packets/s

Header is 32 bytes, followed by `frame_count` frames.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic | ASCII `SLWK` |
| 4 | 1 | version | `1` |
| 5 | 1 | type | `1` = AUDIO |
| 6 | 2 | stream_id | u16, always `0` in v1 |
| 8 | 4 | seq | u32, +1 per packet, wraps |
| 12 | 8 | sample_index | u64, index (since stream start) of the first sample of the **newest** frame |
| 20 | 8 | capture_time_us | u64, sender monotonic clock (µs) at capture of that first sample |
| 28 | 1 | battery | 0–100 %, 255 = unknown |
| 29 | 1 | rssi | i8 dBm, 0 = unknown |
| 30 | 1 | flags | bit0 = muted on phone (samples are zero), bit1 = charging; others 0 |
| 31 | 1 | codec | 1 = PCM16, 2 = PCM24 |
| 32 | … | frames | `frame_count = (len - 32) / bytes_per_frame`, newest first |

Frame `k` (0 = newest) covers samples starting at `sample_index - k*240`. v1 senders send `frame_count = 2`
(current + previous frame, redundancy against single losses) → 992-byte packets with PCM16.
Receivers ignore frames whose samples they already have, and ignore frames with negative start index.

A sender keeps sending (zeros + muted flag) while muted so the receiver keeps clock sync.

## Packet type 2: CONTROL (Mac → phone), 5 packets/s per stream

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic | `SLWK` |
| 4 | 1 | version | `1` |
| 5 | 1 | type | `2` = CONTROL |
| 6 | 2 | stream_id | echoes the stream |
| 8 | 4 | seq | u32, Mac-side counter |
| 12 | 1 | live | 1 = operator has this stream on air (LIVE tally), 0 = READY |
| 13 | 1 | operator_mute | 1 = muted by operator at the Mac |
| 14 | 2 | loss_permille | u16, packets lost (after redundancy) per 1000 over the last ~5 s |
| 16 | 2 | latency_ms | u16, estimated end-to-end latency to the virtual mic, 0 = unknown |
| 18 | 2 | jitter_buffer_ms | u16, current jitter buffer target |
| 20 | 12 | reserved | zero |

Total 32 bytes. The phone treats the receiver as connected while CONTROL packets arrive (timeout 1 s).

## Phone UI state derived from the protocol

- No CONTROL for > 1 s → `NO RECEIVER` (keep sending).
- CONTROL live=0 → `READY`. live=1 → `LIVE`. operator_mute=1 or local mute → `MUTED`.

## Reserved for later versions (do not implement in v1)

- Packet authentication (session key from QR, ChaCha20-Poly1305 tag appended) → bump `version` to 2.
- Codec 3 = Opus CELT restricted-low-delay.
- Multi-phone: different `stream_id` per phone; no wire change needed.
