// Byte-exact implementation of docs/PROTOCOL.md (v1).
#include "slowik_engine.h"

#include <cmath>
#include <cstring>

namespace {

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint64_t rd64(const uint8_t* p) { return uint64_t(rd32(p)) | (uint64_t(rd32(p + 4)) << 32); }
inline void wr16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
inline void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline void wr64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i));
}

}  // namespace

extern "C" int slowik_parse_audio(const uint8_t* d, size_t len, SlowikAudioHeader* h) {
    if (len < SLOWIK_HEADER_BYTES) return SLOWIK_PARSE_TOO_SHORT;
    if (std::memcmp(d, "SLWK", 4) != 0) return SLOWIK_PARSE_BAD_MAGIC;
    if (d[4] != 1) return SLOWIK_PARSE_BAD_VERSION;
    if (d[5] != 1) return SLOWIK_PARSE_BAD_TYPE;
    uint8_t codec = d[31];
    size_t bpf;
    if (codec == 1)
        bpf = SLOWIK_FRAME_SAMPLES * 2;
    else if (codec == 2)
        bpf = SLOWIK_FRAME_SAMPLES * 3;
    else
        return SLOWIK_PARSE_BAD_CODEC;
    int n = int((len - SLOWIK_HEADER_BYTES) / bpf);
    if (n < 1) return SLOWIK_PARSE_NO_FRAMES;
    h->stream_id = rd16(d + 6);
    h->seq = rd32(d + 8);
    h->sample_index = rd64(d + 12);
    h->capture_time_us = rd64(d + 20);
    h->battery = d[28];
    h->rssi = int8_t(d[29]);
    h->flags = d[30];
    h->codec = codec;
    h->frame_count = n;
    h->frames = d + SLOWIK_HEADER_BYTES;
    return SLOWIK_PARSE_OK;
}

extern "C" size_t slowik_build_control(uint8_t* out, size_t cap, uint16_t stream_id, uint32_t seq, int live,
                                       int mute, uint16_t loss_permille, uint16_t latency_ms, uint16_t jb_ms) {
    if (cap < SLOWIK_CONTROL_BYTES) return 0;
    std::memset(out, 0, SLOWIK_CONTROL_BYTES);
    std::memcpy(out, "SLWK", 4);
    out[4] = 1;
    out[5] = 2;
    wr16(out + 6, stream_id);
    wr32(out + 8, seq);
    out[12] = live ? 1 : 0;
    out[13] = mute ? 1 : 0;
    wr16(out + 14, loss_permille);
    wr16(out + 16, latency_ms);
    wr16(out + 18, jb_ms);
    return SLOWIK_CONTROL_BYTES;
}

extern "C" size_t slowik_build_audio(uint8_t* out, size_t cap, uint16_t stream_id, uint32_t seq,
                                     uint64_t sample_index, uint64_t t_us, uint8_t battery, int8_t rssi,
                                     uint8_t flags, uint8_t codec, const float* frames, int frame_count) {
    size_t bps = codec == 2 ? 3 : 2;
    size_t total = SLOWIK_HEADER_BYTES + size_t(frame_count) * SLOWIK_FRAME_SAMPLES * bps;
    if (cap < total) return 0;
    std::memcpy(out, "SLWK", 4);
    out[4] = 1;
    out[5] = 1;
    wr16(out + 6, stream_id);
    wr32(out + 8, seq);
    wr64(out + 12, sample_index);
    wr64(out + 20, t_us);
    out[28] = battery;
    out[29] = uint8_t(rssi);
    out[30] = flags;
    out[31] = codec;
    uint8_t* p = out + SLOWIK_HEADER_BYTES;
    for (int i = 0; i < frame_count * SLOWIK_FRAME_SAMPLES; ++i) {
        float s = frames[i];
        if (s > 1.f) s = 1.f;
        if (s < -1.f) s = -1.f;
        if (codec == 2) {
            int32_t v = int32_t(std::lrint(s * 8388607.0));
            p[0] = uint8_t(v);
            p[1] = uint8_t(v >> 8);
            p[2] = uint8_t(v >> 16);
            p += 3;
        } else {
            int16_t v = int16_t(std::lrint(s * 32767.0));
            wr16(p, uint16_t(v));
            p += 2;
        }
    }
    return total;
}
