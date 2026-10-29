// Slowik Mic - sample-time-indexed loopback ring. No CoreAudio dependency.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace slowik {

// Single-writer / single-reader lock-free ring indexed by absolute sample time
// (frame number on the device timeline). Float32 interleaved.
//
// Timing reasoning (why latency is ~one IO buffer):
//   HAL asks a client that plays to *write* frames [t, t+n) about one IO
//   buffer before wall-clock reaches t, and asks a client that records to
//   *read* frames [t, t+n) about one IO buffer after wall-clock passed t+n
//   (with zero safety offset / latency). So for any frame, the write happens
//   before the read, and the loopback delay is the recorder's buffer size
//   (plus whatever the writer leads by). We therefore read at exactly the
//   same sample time that was written: no extra read delay, no extra latency.
//
// Staleness: `written_` is the end (exclusive) of the newest data written.
//   - Reads at/after `written_` return zeros (writer stopped or is late).
//   - Reads older than `written_ - capacity` return zeros (overwritten).
//   - If the writer jumps forward (pause, then resume) the skipped gap is
//     zero-filled by the writer, so old lap contents never leak.
//   - Clear() resets everything on StartIO/StopIO (timeline restarts).
// A read that races ahead of the writer gets silence for those frames (and
// they are not replayed later); this only happens on underrun.
class LoopbackRing
{
public:
    LoopbackRing(uint32_t capacityFrames, uint32_t channels)
        : cap_(capacityFrames)
        , ch_(channels)
        , data_(new float[(size_t)capacityFrames * channels]())
    {
    }

    uint32_t Capacity() const { return cap_; }

    // Real-time safe. frames: interleaved, n frames, at sample time t.
    void Write(int64_t t, const float* frames, uint32_t n)
    {
        if (n == 0) {
            return;
        }
        if (n > cap_) { // keep only the newest cap_ frames
            frames += (size_t)(n - cap_) * ch_;
            t += n - cap_;
            n = cap_;
        }
        const int64_t end = t + n;
        const int64_t w = written_.load(std::memory_order_relaxed);
        if (w > 0 && t > w) { // gap: zero it so old laps can't leak
            ZeroRange(std::max<int64_t>(w, t - cap_), t);
        } else if (w == 0 && t > 0) {
            ZeroRange(std::max<int64_t>(0, t - cap_), t);
        }
        CopyIn(t, frames, n);
        if (end > w || !valid_.load(std::memory_order_relaxed)) {
            written_.store(end, std::memory_order_release);
            valid_.store(true, std::memory_order_release);
        }
    }

    // Real-time safe. Fills `out` with n frames starting at sample time t;
    // frames with no valid data are zero.
    void Read(int64_t t, float* out, uint32_t n) const
    {
        std::memset(out, 0, (size_t)n * ch_ * sizeof(float));
        if (!valid_.load(std::memory_order_acquire)) {
            return;
        }
        const int64_t w = written_.load(std::memory_order_acquire);
        const int64_t lo = std::max<int64_t>(t, w - (int64_t)cap_);
        const int64_t hi = std::min<int64_t>(t + n, w);
        for (int64_t f = lo; f < hi; f++) {
            std::memcpy(out + (size_t)(f - t) * ch_, Slot(f), ch_ * sizeof(float));
        }
    }

    // Not real-time safe w.r.t. concurrent IO; call from StartIO/StopIO.
    void Clear()
    {
        valid_.store(false, std::memory_order_release);
        written_.store(0, std::memory_order_release);
        std::memset(data_.get(), 0, (size_t)cap_ * ch_ * sizeof(float));
    }

private:
    float* Slot(int64_t f) const
    {
        return data_.get() + (size_t)(((f % cap_) + cap_) % cap_) * ch_;
    }
    void CopyIn(int64_t t, const float* frames, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++) {
            std::memcpy(Slot(t + i), frames + (size_t)i * ch_, ch_ * sizeof(float));
        }
    }
    void ZeroRange(int64_t from, int64_t to)
    {
        for (int64_t f = from; f < to; f++) {
            std::memset(Slot(f), 0, ch_ * sizeof(float));
        }
    }

    const uint32_t cap_;
    const uint32_t ch_;
    std::unique_ptr<float[]> data_;
    std::atomic<int64_t> written_ { 0 };
    std::atomic<bool> valid_ { false };
};

} // namespace slowik
