// Lock-free single-producer / single-consumer float ring. Capacity must be a power of two.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

namespace slowik {

class SpscRing {
public:
    explicit SpscRing(size_t capacityPow2) : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

    // Producer. Returns number of samples actually written (drops the rest when full).
    size_t write(const float* src, size_t n) {
        const uint64_t w = write_.load(std::memory_order_relaxed);
        const uint64_t r = read_.load(std::memory_order_acquire);
        const size_t free = buf_.size() - static_cast<size_t>(w - r);
        if (n > free) n = free;
        for (size_t i = 0; i < n; i++) buf_[(w + i) & mask_] = src[i];
        write_.store(w + n, std::memory_order_release);
        return n;
    }

    size_t available() const {
        return static_cast<size_t>(write_.load(std::memory_order_acquire) - read_.load(std::memory_order_relaxed));
    }

    // Consumer. Reads exactly n samples if available, else returns false.
    bool read(float* dst, size_t n) {
        const uint64_t r = read_.load(std::memory_order_relaxed);
        if (available() < n) return false;
        for (size_t i = 0; i < n; i++) dst[i] = buf_[(r + i) & mask_];
        read_.store(r + n, std::memory_order_release);
        return true;
    }

    // Consumer-side only.
    void clear() { read_.store(write_.load(std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<float> buf_;
    const size_t mask_;
    std::atomic<uint64_t> write_{0};
    std::atomic<uint64_t> read_{0};
};

}  // namespace slowik
