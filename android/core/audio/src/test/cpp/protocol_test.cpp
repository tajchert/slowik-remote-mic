#include "../../main/cpp/slowik_protocol.h"
#include <cstdio>
#include <vector>
using namespace slowik;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    CHECK(kAudioPacketSize == 992);
    std::vector<int16_t> a(kFrameSamples), b(kFrameSamples);
    for (size_t i = 0; i < kFrameSamples; i++) { a[i] = (int16_t)(1000 + i); b[i] = (int16_t)(-2000 - (int)i); }
    a[0] = 0x1234; b[0] = -2;
    AudioMeta m{0x01020304u, 0x0A0B0C0D0E0F1011ull, 0x1112131415161718ull, 77, -61, kFlagCharging};
    uint8_t buf[kAudioPacketSize];
    CHECK(encodeAudio(buf, m, a.data(), b.data()) == 992);
    CHECK(std::memcmp(buf, "SLWK", 4) == 0);
    CHECK(buf[4] == 1 && buf[5] == 1);
    CHECK(buf[6] == 0 && buf[7] == 0);
    const uint8_t seq[] = {4, 3, 2, 1};
    CHECK(std::memcmp(buf + 8, seq, 4) == 0);
    const uint8_t si[] = {0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A};
    CHECK(std::memcmp(buf + 12, si, 8) == 0);
    const uint8_t ct[] = {0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11};
    CHECK(std::memcmp(buf + 20, ct, 8) == 0);
    CHECK(buf[28] == 77);
    CHECK(buf[29] == (uint8_t)-61);
    CHECK(buf[30] == 2);
    CHECK(buf[31] == 1);
    // newest frame first, little-endian samples
    CHECK(buf[32] == 0x34 && buf[33] == 0x12);
    CHECK(get16(buf + 32 + 2) == (uint16_t)a[1]);
    // previous frame starts after 480 bytes
    CHECK(get16(buf + 32 + 480) == (uint16_t)-2);
    CHECK(get16(buf + 32 + 480 + 2) == (uint16_t)b[1]);

    // CONTROL decode (matches struct "<4sBBHIBBHHH12x")
    uint8_t c[32] = {'S', 'L', 'W', 'K', 1, 2, 0, 0, 9, 0, 0, 0, 1, 0, 0x2C, 0x01, 0x28, 0, 0x14, 0};
    Control ctl;
    CHECK(decodeControl(c, 32, &ctl));
    CHECK(ctl.seq == 9 && ctl.live == 1 && ctl.operatorMute == 0);
    CHECK(ctl.lossPermille == 300 && ctl.latencyMs == 40 && ctl.jitterBufferMs == 20);
    c[5] = 1;
    CHECK(!decodeControl(c, 32, &ctl));
    c[5] = 2;
    CHECK(!decodeControl(c, 31, &ctl));

    CHECK(floatToPcm16(1.5f) == 32767);
    CHECK(floatToPcm16(-1.5f) == -32767);
    CHECK(floatToPcm16(0.0f) == 0);
    CHECK(floatToPcm16(0.5f) == 16383);
    if (fails == 0) std::printf("protocol tests OK\n");
    return fails ? 1 : 0;
}
