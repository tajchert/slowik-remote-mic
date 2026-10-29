// Deterministic network + clock simulator driving a SlowikEngine (no sleeping, no threads).
// Used by the unit tests; time is simulated in microseconds.
#ifndef SLOWIK_SIM_H
#define SLOWIK_SIM_H

#include <stdint.h>

#include "slowik_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double seconds;          // simulated duration
    double freq, amp;        // sine source
    double loss;             // random packet loss probability
    double jitter_ms;        // uniform extra delay 0..jitter_ms per packet (causes reordering)
    double ppm;              // sender clock skew: >0 = sender slower (same convention as slowik_sim.py)
    double dup_prob;         // duplicate packet probability
    int frames_per_packet;   // 2 in v1
    int codec;               // 1 or 2
    int render_block;        // frames per render call (e.g. 128)
    uint32_t seed;
    uint32_t seq_start;      // e.g. 0xFFFFFF00 to test wrap
    // jitter burst: extra uniform 0..burst_ms delay in [burst_at, burst_at+burst_len)
    double burst_at_s, burst_len_s, burst_ms;
    // outage: no packets in [outage_at, outage_at+outage_len) (sender keeps its clock running)
    double outage_at_s, outage_len_s;
    // sender restart at this time: seq and sample_index restart from 0 (0 = never)
    double restart_at_s;
    double warmup_s;         // stats below exclude the first warmup_s seconds
    const char* wav_path;    // optional: write rendered output (mono float->PCM16)
} SlowikSimConfig;

typedef struct {
    SlowikStats stats;           // final engine stats
    uint64_t underruns_after_warmup;
    uint64_t concealed_after_warmup;
    double fill_err_max_ms;      // max |fill - target| after warmup (+ 10 s settle)
    double fill_max_ms, fill_min_ms;
    double target_max_ms;
    uint64_t discontinuities;    // |x[n]-x[n-1]| > threshold after warmup, while connected
    double max_step;             // largest sample-to-sample step after warmup
    double max_abs_while_lost;   // largest output sample while link state == LOST (after fade)
    int saw_link_lost;
    double link_lost_at_s;
    double recovered_at_s;       // first time CONNECTED again after LOST
    double drift_ppm_final;
} SlowikSimResult;

void slowik_sim_default(SlowikSimConfig* c);
int slowik_sim_run(const SlowikSimConfig* c, const SlowikConfig* engine_cfg /* nullable */, SlowikSimResult* r);

#ifdef __cplusplus
}
#endif

#endif
