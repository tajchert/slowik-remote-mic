#!/usr/bin/env python3
"""Reference implementation of the Slowik v1 wire protocol (docs/PROTOCOL.md).

  send     -- act as a phone: stream a sine tone (or a WAV file) to a receiver, print CONTROL replies.
  receive  -- act as a Mac: accept AUDIO packets, print stats, reply with CONTROL, optionally write a WAV.

Examples:
  python3 tools/slowik_sim.py send --host 127.0.0.1 --seconds 10 --loss 0.02 --jitter-ms 8
  python3 tools/slowik_sim.py receive --wav /tmp/out.wav
"""
import argparse
import math
import random
import socket
import struct
import sys
import threading
import time
import wave

MAGIC = b"SLWK"
VERSION = 1
TYPE_AUDIO = 1
TYPE_CONTROL = 2
PORT = 47800
RATE = 48000
FRAME = 240
CODEC_PCM16 = 1
CODEC_PCM24 = 2
AUDIO_HDR = struct.Struct("<4sBBHIQQBbBB")  # 32 bytes
CONTROL = struct.Struct("<4sBBHIBBHHH12x")  # 32 bytes
assert AUDIO_HDR.size == 32 and CONTROL.size == 32


def pack_audio(stream_id, seq, sample_index, t_us, frames, battery=255, rssi=0, flags=0, codec=CODEC_PCM16):
    hdr = AUDIO_HDR.pack(MAGIC, VERSION, TYPE_AUDIO, stream_id, seq & 0xFFFFFFFF, sample_index, t_us,
                         battery, rssi, flags, codec)
    return hdr + b"".join(frames)


def unpack_audio(data):
    if len(data) < 32:
        return None
    magic, ver, typ, sid, seq, sidx, t_us, batt, rssi, flags, codec = AUDIO_HDR.unpack_from(data)
    if magic != MAGIC or ver != VERSION or typ != TYPE_AUDIO:
        return None
    bpf = FRAME * (2 if codec == CODEC_PCM16 else 3)
    n = (len(data) - 32) // bpf
    frames = [data[32 + i * bpf:32 + (i + 1) * bpf] for i in range(n)]
    return dict(stream_id=sid, seq=seq, sample_index=sidx, t_us=t_us, battery=batt, rssi=rssi,
                flags=flags, codec=codec, frames=frames)


def pack_control(stream_id, seq, live, mute, loss_permille=0, latency_ms=0, jb_ms=0):
    return CONTROL.pack(MAGIC, VERSION, TYPE_CONTROL, stream_id, seq & 0xFFFFFFFF, live, mute,
                        loss_permille, latency_ms, jb_ms)


def unpack_control(data):
    if len(data) < 32:
        return None
    magic, ver, typ, sid, seq, live, mute, loss, lat, jb = CONTROL.unpack_from(data)
    if magic != MAGIC or ver != VERSION or typ != TYPE_CONTROL:
        return None
    return dict(stream_id=sid, seq=seq, live=live, mute=mute, loss_permille=loss, latency_ms=lat, jb_ms=jb)


def pcm16_frame(samples):
    return struct.pack("<%dh" % len(samples), *(max(-32768, min(32767, int(s * 32767))) for s in samples))


def cmd_send(a):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (a.host, a.port)
    stop = threading.Event()

    def rx():
        sock.settimeout(0.2)
        last = None
        while not stop.is_set():
            try:
                data, _ = sock.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            c = unpack_control(data)
            if c:
                key = {k: v for k, v in c.items() if k != "seq"}
                if key != last:
                    print("CONTROL", c, flush=True)
                    last = key

    src = None
    if a.wav:
        w = wave.open(a.wav, "rb")
        assert w.getframerate() == RATE and w.getsampwidth() == 2, "WAV must be 48 kHz 16-bit"
        ch = w.getnchannels()
        raw = w.readframes(w.getnframes())
        allv = struct.unpack("<%dh" % (len(raw) // 2), raw)
        src = [v / 32768.0 for v in allv[::ch]]

    threading.Thread(target=rx, daemon=True).start()
    # Skew the sender clock by --ppm to exercise the receiver's drift compensation.
    period = FRAME / RATE * (1 + a.ppm / 1e6)
    prev = b"\0" * (FRAME * 2)
    t0 = time.monotonic()
    total = int(a.seconds * RATE / FRAME)
    pending = []
    for i in range(total):
        sidx = i * FRAME
        if src is not None:
            chunk = [src[(sidx + k) % len(src)] for k in range(FRAME)]
        else:
            chunk = [a.amp * math.sin(2 * math.pi * a.freq * (sidx + k) / RATE) for k in range(FRAME)]
        cur = pcm16_frame(chunk)
        pkt = pack_audio(0, i, sidx, int(time.monotonic() * 1e6), [cur, prev] if i else [cur], battery=80, rssi=-50)
        prev = cur
        if random.random() >= a.loss:
            delay = random.uniform(0, a.jitter_ms / 1000.0)
            if sidx * 1000.0 / RATE < a.burst_start_ms:
                # Wi-Fi power-save wake-up: hold the first N ms of packets, then send them all at once.
                pending.append((t0 + a.burst_start_ms / 1000.0, pkt))
            else:
                pending.append((time.monotonic() + delay, pkt))
        now = time.monotonic()
        pending.sort(key=lambda x: x[0])
        while pending and pending[0][0] <= now:
            sock.sendto(pending.pop(0)[1], dest)
        target = t0 + (i + 1) * period
        sl = target - time.monotonic()
        if sl > 0:
            time.sleep(sl)
    stop.set()
    print("sent %d packets" % total)


def cmd_receive(a):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", a.port))
    sock.settimeout(0.2)
    print("listening on udp/%d" % a.port)
    wav = None
    if a.wav:
        wav = wave.open(a.wav, "wb")
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
    have = {}
    peer = None
    ctl_seq = 0
    last_ctl = 0.0
    last_stat = time.monotonic()
    got = lost_seq = 0
    max_sidx = -1
    try:
        while True:
            try:
                data, addr = sock.recvfrom(2048)
                p = unpack_audio(data)
                if p:
                    peer = addr
                    got += 1
                    for k, f in enumerate(p["frames"]):
                        start = p["sample_index"] - k * FRAME
                        if start >= 0 and start not in have:
                            have[start] = f
                    max_sidx = max(max_sidx, p["sample_index"])
            except socket.timeout:
                pass
            now = time.monotonic()
            if peer and now - last_ctl >= 0.2:
                sock.sendto(pack_control(0, ctl_seq, 1, 0, 0, 0, 0), peer)
                ctl_seq += 1
                last_ctl = now
            if now - last_stat >= 1.0:
                expected = max_sidx // FRAME + 1 if max_sidx >= 0 else 0
                missing = expected - len(have)
                print("packets=%d frames=%d missing_after_redundancy=%d" % (got, len(have), missing), flush=True)
                last_stat = now
    except KeyboardInterrupt:
        pass
    finally:
        if wav:
            for start in sorted(have):
                wav.writeframes(have[start])
            wav.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("send")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=PORT)
    s.add_argument("--seconds", type=float, default=10)
    s.add_argument("--freq", type=float, default=440)
    s.add_argument("--amp", type=float, default=0.3)
    s.add_argument("--wav", help="48 kHz 16-bit WAV to loop instead of a sine")
    s.add_argument("--loss", type=float, default=0.0, help="random packet loss probability")
    s.add_argument("--jitter-ms", type=float, default=0.0, help="max random extra delay per packet")
    s.add_argument("--ppm", type=float, default=0.0, help="sender clock skew in ppm")
    s.add_argument("--burst-start-ms", type=float, default=0.0,
                   help="withhold the first N ms of packets and send them at once (start-up burst)")
    r = sub.add_parser("receive")
    r.add_argument("--port", type=int, default=PORT)
    r.add_argument("--wav", help="write received audio here on Ctrl-C")
    a = ap.parse_args()
    {"send": cmd_send, "receive": cmd_receive}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
