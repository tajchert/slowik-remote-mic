// Slowik receiver engine: packet parsing, jitter buffer, concealment, drift-compensating resampler.
// Plain C API (implemented in C++17) so Swift can import it without C++ interop.
//
// Threading contract:
//   * slowik_engine_push_packet      -- ONE network thread (single producer).
//   * slowik_engine_render           -- ONE real-time audio thread (single consumer). Lock/alloc/syscall free.
//   * set_* / get_stats / build_control -- any thread (atomics only).
#ifndef SLOWIK_ENGINE_H
#define SLOWIK_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLOWIK_SAMPLE_RATE 48000
#define SLOWIK_FRAME_SAMPLES 240
#define SLOWIK_DEFAULT_PORT 47800
#define SLOWIK_HEADER_BYTES 32
#define SLOWIK_CONTROL_BYTES 32

enum {
    SLOWIK_LINK_WAITING = 0,   // never received a packet
    SLOWIK_LINK_CONNECTED = 1, // packets flowing
    SLOWIK_LINK_LOST = 2,      // no packets for > link_lost_ms, output faded to silence
};

enum {
    SLOWIK_PARSE_OK = 0,
    SLOWIK_PARSE_TOO_SHORT = -1,
    SLOWIK_PARSE_BAD_MAGIC = -2,
    SLOWIK_PARSE_BAD_VERSION = -3,
    SLOWIK_PARSE_BAD_TYPE = -4,
    SLOWIK_PARSE_BAD_CODEC = -5,
    SLOWIK_PARSE_NO_FRAMES = -6,
    SLOWIK_PUSH_QUEUE_FULL = -10,
    SLOWIK_PUSH_TOO_MANY_STREAMS = -11,
};

typedef struct {
    uint16_t stream_id;
    uint32_t seq;
    uint64_t sample_index;     // first sample of the newest frame
    uint64_t capture_time_us;
    uint8_t battery;           // 0-100, 255 unknown
    int8_t rssi;               // dBm, 0 unknown
    uint8_t flags;             // bit0 muted on phone, bit1 charging
    uint8_t codec;             // 1 PCM16, 2 PCM24
    int frame_count;
    const uint8_t* frames;     // points into the packet, newest first
} SlowikAudioHeader;

typedef struct {
    double min_target_ms;      // 10
    double max_target_ms;      // 80
    double initial_target_ms;  // 25 (used until enough jitter history exists)
    double margin_ms;          // 3
    double max_ppm;            // 200 (steady-state resampler ratio clamp)
    double link_lost_ms;       // 150
    double kp;                 // PI proportional gain (ratio per second of fill error)
    double ki;                 // PI integral gain (ratio per second^2)
} SlowikConfig;

typedef struct {
    // network thread
    uint64_t packets_received;
    uint64_t packets_invalid;
    uint64_t packets_dropped_queue;
    uint64_t packets_lost_seq;    // before redundancy, from seq gaps
    // render thread
    uint64_t frames_played;
    uint64_t frames_concealed;    // lost after redundancy (or arrived too late)
    uint64_t frames_late;         // arrived after their playout time
    uint64_t frames_duplicate;    // duplicate packets (same seq seen recently)
    uint64_t frames_dropped;      // dropped from the buffer head to remove excess latency (bursts)
    uint64_t underruns;           // buffer ran empty -> stalled with concealment
    uint64_t resyncs;
    uint64_t link_lost_events;
    double loss_permille;         // after redundancy, last ~5 s
    double jitter_ms;             // p99 - mean of first-arrival transit (last 10 s)
    double jitter_mean_ms;        // mean - min of transit
    double target_ms;             // jitter buffer target
    double fill_ms;               // smoothed jitter buffer fill
    double drift_ppm;             // learned resampler offset; >0 = phone clock faster than output clock
    double ratio_ppm;             // instantaneous resampler ratio offset
    double latency_ms;            // estimated capture -> output latency
    float peak;                   // engine output (pre-DSP) peak, linear, last ~50 ms
    float rms;                    // engine output RMS, linear, last ~50 ms
    int battery;                  // -1 unknown
    int rssi;                     // dBm, 0 unknown
    int flags;
    int codec;
    int link_state;               // SLOWIK_LINK_*
    int stream_id;                // selected stream (-1 none)
    int streams_seen;
    int render_block;             // largest render() frame count seen
} SlowikStats;

typedef struct SlowikEngine SlowikEngine;

void slowik_config_default(SlowikConfig* cfg);

SlowikEngine* slowik_engine_create(const SlowikConfig* cfg /* nullable = defaults */);
void slowik_engine_destroy(SlowikEngine* e);

// Network thread. arrival_us: monotonic receive time in microseconds.
int slowik_engine_push_packet(SlowikEngine* e, const uint8_t* data, size_t len, uint64_t arrival_us);

// Real-time thread. Writes n mono float samples at 48 kHz.
void slowik_engine_render(SlowikEngine* e, float* out, int n);

void slowik_engine_get_stats(SlowikEngine* e, SlowikStats* out);

void slowik_engine_set_live(SlowikEngine* e, int live);
void slowik_engine_set_mute(SlowikEngine* e, int mute);              // operator mute (5 ms fade)
void slowik_engine_set_output_latency_ms(SlowikEngine* e, double ms); // device + IO buffer, for the estimate

// Builds the 32-byte CONTROL packet for the selected stream from current state. Returns bytes written (32).
size_t slowik_engine_build_control(SlowikEngine* e, uint8_t* out, size_t cap);

// Stateless helpers (also used by tests).
int slowik_parse_audio(const uint8_t* data, size_t len, SlowikAudioHeader* out);
size_t slowik_build_control(uint8_t* out, size_t cap, uint16_t stream_id, uint32_t seq, int live, int mute,
                            uint16_t loss_permille, uint16_t latency_ms, uint16_t jitter_buffer_ms);
// Builds an AUDIO packet (PCM16 or PCM24) from float frames (newest first, frame_count*240 samples).
size_t slowik_build_audio(uint8_t* out, size_t cap, uint16_t stream_id, uint32_t seq, uint64_t sample_index,
                          uint64_t capture_time_us, uint8_t battery, int8_t rssi, uint8_t flags, uint8_t codec,
                          const float* frames, int frame_count);

#ifdef __cplusplus
}
#endif

#endif
