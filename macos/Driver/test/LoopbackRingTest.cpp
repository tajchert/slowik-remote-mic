#include "../src/LoopbackRing.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

using slowik::LoopbackRing;

static std::vector<float> ramp(int start, int n) // 2ch: L=v, R=-v
{
    std::vector<float> v;
    for (int i = 0; i < n; i++) { v.push_back(start + i + 1); v.push_back(-(start + i + 1)); }
    return v;
}
static std::vector<float> rd(const LoopbackRing& r, int64_t t, int n)
{
    std::vector<float> o(n * 2, 9.f);
    r.Read(t, o.data(), n);
    return o;
}
static bool allZero(const std::vector<float>& v) { for (float f : v) if (f != 0) return false; return true; }

int main()
{
    { // basic write/read
        LoopbackRing r(1024, 2);
        auto w = ramp(100, 64);
        r.Write(100, w.data(), 64);
        CHECK(rd(r, 100, 64) == w);
        auto part = rd(r, 110, 8);
        CHECK(part[0] == 111.f && part[1] == -111.f);
    }
    { // empty ring is silence
        LoopbackRing r(1024, 2);
        CHECK(allZero(rd(r, 0, 128)));
    }
    { // wraparound
        LoopbackRing r(256, 2);
        for (int64_t t = 0; t < 2000; t += 100) {
            auto w = ramp((int)t, 100);
            r.Write(t, w.data(), 100);
            CHECK(rd(r, t, 100) == w);
        }
    }
    { // stale -> silence once the writer stops
        LoopbackRing r(1024, 2);
        auto w = ramp(0, 512);
        r.Write(0, w.data(), 512);
        CHECK(!allZero(rd(r, 0, 512)));
        CHECK(allZero(rd(r, 512, 512)));     // past last written
        CHECK(allZero(rd(r, 5000, 512)));    // far in the future
    }
    { // data older than capacity is not served
        LoopbackRing r(256, 2);
        for (int64_t t = 0; t < 1024; t += 128) { auto w = ramp((int)t, 128); r.Write(t, w.data(), 128); }
        CHECK(allZero(rd(r, 0, 128)));
        CHECK(!allZero(rd(r, 1024 - 128, 128)));
    }
    { // pause then resume: gap must be zeros, not old lap data
        LoopbackRing r(256, 2);
        auto a = ramp(0, 256);
        r.Write(0, a.data(), 256);
        auto b = ramp(1000, 64);
        r.Write(1000, b.data(), 64);
        CHECK(allZero(rd(r, 800, 128)));
        CHECK(rd(r, 1000, 64) == b);
    }
    { // read-before-write: reader ahead sees silence, then data once written
        LoopbackRing r(1024, 2);
        auto w = ramp(0, 128);
        CHECK(allZero(rd(r, 0, 128)));
        r.Write(0, w.data(), 128);
        CHECK(rd(r, 0, 128) == w);
        // partial overlap: reader window straddles written end
        auto o = rd(r, 64, 128);
        CHECK(o[0] == 65.f && o[2 * 63] == 128.f && o[2 * 64] == 0.f);
    }
    { // Clear() drops old audio and restarts timeline
        LoopbackRing r(1024, 2);
        auto w = ramp(0, 128);
        r.Write(0, w.data(), 128);
        r.Clear();
        CHECK(allZero(rd(r, 0, 128)));
        auto w2 = ramp(500, 32);
        r.Write(0, w2.data(), 32);
        CHECK(rd(r, 0, 32) == w2);
    }
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
