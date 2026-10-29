// Slowik wire protocol v1 (see docs/PROTOCOL.md). Pure C++17, no Android deps.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace slowik {

constexpr size_t kHeaderSize = 32;
constexpr size_t kFrameSamples = 240;
constexpr size_t kFrameBytesPcm16 = kFrameSamples * 2;
constexpr int kRedundancy = 2;
constexpr size_t kAudioPacketSize = kHeaderSize + kRedundancy * kFrameBytesPcm16;  // 992
constexpr uint8_t kVersion = 1;
constexpr uint8_t kTypeAudio = 1;
constexpr uint8_t kTypeControl = 2;
constexpr uint8_t kFlagMuted = 1;
constexpr uint8_t kFlagCharging = 2;
constexpr uint8_t kCodecPcm16 = 1;

inline void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
inline void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (v >> (8 * i)) & 0xFF; }
inline uint16_t get16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }
inline uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }

inline int16_t floatToPcm16(float f) {
    if (!(f == f)) return 0;  // NaN
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return static_cast<int16_t>(f * 32767.0f);
}

struct AudioMeta {
    uint32_t seq;
    uint64_t sampleIndex;  // first sample of the newest frame
    uint64_t captureTimeUs;
    uint8_t battery;       // 255 unknown
    int8_t rssi;           // 0 unknown
    uint8_t flags;
};

// newest/previous: kFrameSamples PCM16 samples each. out must hold kAudioPacketSize bytes.
inline size_t encodeAudio(uint8_t* out, const AudioMeta& m, const int16_t* newest, const int16_t* previous) {
    std::memcpy(out, "SLWK", 4);
    out[4] = kVersion;
    out[5] = kTypeAudio;
    put16(out + 6, 0);
    put32(out + 8, m.seq);
    put64(out + 12, m.sampleIndex);
    put64(out + 20, m.captureTimeUs);
    out[28] = m.battery;
    out[29] = static_cast<uint8_t>(m.rssi);
    out[30] = m.flags;
    out[31] = kCodecPcm16;
    uint8_t* p = out + kHeaderSize;
    const int16_t* frames[2] = {newest, previous};
    for (const int16_t* f : frames)
        for (size_t i = 0; i < kFrameSamples; i++, p += 2) put16(p, static_cast<uint16_t>(f[i]));
    return kAudioPacketSize;
}

struct Control {
    uint16_t streamId;
    uint32_t seq;
    uint8_t live;
    uint8_t operatorMute;
    uint16_t lossPermille;
    uint16_t latencyMs;
    uint16_t jitterBufferMs;
};

inline bool decodeControl(const uint8_t* d, size_t len, Control* c) {
    if (len < kHeaderSize || std::memcmp(d, "SLWK", 4) != 0 || d[4] != kVersion || d[5] != kTypeControl) return false;
    c->streamId = get16(d + 6);
    c->seq = get32(d + 8);
    c->live = d[12];
    c->operatorMute = d[13];
    c->lossPermille = get16(d + 14);
    c->latencyMs = get16(d + 16);
    c->jitterBufferMs = get16(d + 18);
    return true;
}

}  // namespace slowik
