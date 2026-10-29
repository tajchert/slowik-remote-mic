#!/usr/bin/env python3
"""Sanity-check a slowik-rx WAV of the simulator sine: frequency, level, discontinuities, dropouts.
usage: python3 scripts/wavcheck.py out.wav   (needs numpy)"""
import sys, wave, numpy as np
w = wave.open(sys.argv[1]); sr = w.getframerate(); ch = w.getnchannels()
x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)[::ch] / 32768
nz = np.flatnonzero(np.abs(x) > 1e-3)
a, b = nz[0] + sr, nz[-1] - int(0.2 * sr)       # skip first second after audio starts and the fade-out
seg = x[a:b]
print(f"duration={len(x)/sr:.1f}s audio={nz[0]/sr:.2f}..{nz[-1]/sr:.2f}s analysed={len(seg)/sr:.1f}s")
win = np.hanning(len(seg)); sp = np.abs(np.fft.rfft(seg * win)); k = np.argmax(sp)
# parabolic interpolation of the peak
d = 0.5 * (sp[k-1] - sp[k+1]) / (sp[k-1] - 2*sp[k] + sp[k+1]); f = (k + d) * sr / len(seg)
print(f"peak freq={f:.3f} Hz  (440 Hz sent; +150 ppm slower sender + resampling ⇒ {440/(1+150e-6):.3f})")
print(f"rms={20*np.log10(np.sqrt(np.mean(seg**2))):.2f} dBFS (0.3 sine = {20*np.log10(0.3/np.sqrt(2)):.2f}) peak={20*np.log10(np.max(np.abs(seg))):.2f} dBFS")
step = np.abs(np.diff(seg)); thr = 3 * 0.3 * 2 * np.pi * 440 / sr
print(f"max step={step.max():.4f} (sine max {0.3*2*np.pi*440/sr:.4f}); discontinuities(>{thr:.3f})={int((step>thr).sum())}")
# exact-zero runs (hard dropouts) of >= 24 samples inside the analysed region
z = (np.abs(seg) < 1e-4).astype(np.int8); runs = 0; r = 0
for v in z:
    r = r + 1 if v else 0
    if r == 24: runs += 1
print(f"silent runs >=0.5ms: {runs}")
# residual vs a sine fitted per 100 ms block (tracking ±400 ppm): concealment / resampler artefacts
blk = sr // 10
vals = []
for i in range(0, len(seg) - blk, blk):
    s = seg[i:i+blk]; t = np.arange(blk) / sr
    best = None
    for ff in np.linspace(f*(1-400e-6), f*(1+400e-6), 17):   # allow the resampler's ±200 ppm (+ sender jitter)
        M = np.stack([np.sin(2*np.pi*ff*t), np.cos(2*np.pi*ff*t)], 1); c, *_ = np.linalg.lstsq(M, s, rcond=None)
        r = np.sqrt(np.mean((s - M @ c)**2)); best = r if best is None else min(best, r)
    vals.append(20*np.log10(best/0.212 + 1e-12))
vals = np.array(vals)
print("residual (freq-tracked) percentiles dB: p50=%.1f p90=%.1f p99=%.1f max=%.1f; blocks > -40 dB: %d/%d" % (np.percentile(vals,50), np.percentile(vals,90), np.percentile(vals,99), vals.max(), (vals>-40).sum(), len(vals)))
print("blocks > -40 dB at t[s]:", [round(float((a + i*blk)/sr), 1) for i in np.flatnonzero(vals > -40)])
