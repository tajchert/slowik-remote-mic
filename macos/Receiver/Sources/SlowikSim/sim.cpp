#include "slowik_sim.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <queue>
#include <random>
#include <vector>

namespace {

struct Arrival {
    double t_us;
    uint64_t order;
    std::vector<uint8_t> pkt;
    uint64_t key;
    bool operator>(const Arrival& o) const { return t_us != o.t_us ? t_us > o.t_us : order > o.order; }
};

void write_wav(const char* path, const std::vector<float>& s) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    uint32_t data = uint32_t(s.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    w32(36 + data);
    std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16(1);
    w32(48000);
    w32(96000);
    w16(2);
    w16(16);
    std::fwrite("data", 1, 4, f);
    w32(data);
    for (float x : s) {
        float c = std::fmax(-1.f, std::fmin(1.f, x));
        int16_t v = int16_t(std::lrint(c * 32767));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

}  // namespace

extern "C" void slowik_sim_default(SlowikSimConfig* c) {
    std::memset(c, 0, sizeof(*c));
    c->seconds = 10;
    c->freq = 440;
    c->amp = 0.3;
    c->frames_per_packet = 2;
    c->codec = 1;
    c->render_block = 128;
    c->seed = 1;
    c->warmup_s = 2;
    c->settle_s = 10;
}

extern "C" int slowik_sim_run(const SlowikSimConfig* c, const SlowikConfig* ecfg, SlowikSimResult* r) {
    std::memset(r, 0, sizeof(*r));
    r->fill_min_ms = 1e9;
    r->link_lost_at_s = -1;
    r->recovered_at_s = -1;
    SlowikEngine* e = slowik_engine_create(ecfg);
    if (!e) return -1;

    std::mt19937 rng(c->seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::priority_queue<Arrival, std::vector<Arrival>, std::greater<Arrival>> net;
    uint64_t order = 0;

    const double period_us = 5000.0 * (1.0 + c->ppm / 1e6);
    const double end_us = c->seconds * 1e6;
    const int fpp = c->frames_per_packet;
    std::vector<float> history;  // sender samples (since its last restart)
    std::vector<float> frames(size_t(fpp) * 240);
    std::vector<uint8_t> buf(32 + size_t(fpp) * 240 * 3);
    uint64_t pkt_i = 0, local_i = 0;
    bool restarted = false;
    double next_send = 0;

    auto send_until = [&](double t_us) {
        while (next_send <= t_us) {
            double ts = next_send;
            if (c->restart_at_s > 0 && !restarted && ts >= c->restart_at_s * 1e6) {
                restarted = true;
                local_i = 0;
                history.clear();
            }
            uint64_t sidx = local_i * 240;
            // Generate this frame's samples (phase continuous over the stream's sample index).
            for (int k = 0; k < 240; ++k)
                history.push_back(float(c->amp * std::sin(2 * M_PI * c->freq * double(sidx + k) / 48000.0)));
            int nf = 0;
            for (int k = 0; k < fpp; ++k) {
                if (int64_t(sidx) - int64_t(k) * 240 < 0) break;
                std::memcpy(&frames[size_t(k) * 240], &history[size_t(local_i - k) * 240], 240 * sizeof(float));
                ++nf;
            }
            uint32_t seq = uint32_t(c->seq_start + (restarted ? local_i : pkt_i));
            size_t len = slowik_build_audio(buf.data(), buf.size(), 0, seq, sidx, uint64_t(ts), 80, -50, 0,
                                            uint8_t(c->codec), frames.data(), nf);
            bool drop = U(rng) < c->loss;
            if (c->outage_len_s > 0 && ts >= c->outage_at_s * 1e6 && ts < (c->outage_at_s + c->outage_len_s) * 1e6)
                drop = true;
            if (!drop) {
                double d = 1000.0 + U(rng) * c->jitter_ms * 1000.0;
                if (c->burst_len_s > 0 && ts >= c->burst_at_s * 1e6 && ts < (c->burst_at_s + c->burst_len_s) * 1e6)
                    d += U(rng) * c->burst_ms * 1000.0;
                double at = ts + d;
                if (ts < c->burst_start_ms * 1e3) at = c->burst_start_ms * 1e3 + U(rng) * 500.0;
                double s0 = c->stall_at_s * 1e6, s1 = (c->stall_at_s + c->stall_len_s) * 1e6;
                if (c->stall_len_s > 0 && at >= s0 && at < s1) at = s1 + U(rng) * 500.0;
                if (c->arrival_quantum_ms > 0) {
                    double q = c->arrival_quantum_ms * 1e3;
                    at = std::ceil(at / q) * q;
                }
                net.push(Arrival{at, order++, std::vector<uint8_t>(buf.begin(), buf.begin() + long(len)), 1});
                if (U(rng) < c->dup_prob)
                    net.push(Arrival{ts + d + U(rng) * 2000.0, order++,
                                     std::vector<uint8_t>(buf.begin(), buf.begin() + long(len)), 1});
                int64_t off = int64_t(c->second_sender_offset_ms * 48);
                if (off > 0 && int64_t(sidx) >= off) {
                    // same audio, other socket: different seq space and lagging sample_index
                    std::vector<uint8_t> p2(buf.begin(), buf.begin() + long(len));
                    uint32_t s2 = seq + 777777;
                    uint64_t i2 = sidx - uint64_t(off);
                    std::memcpy(&p2[8], &s2, 4);
                    std::memcpy(&p2[12], &i2, 8);
                    net.push(Arrival{ts + 1000.0 + U(rng) * c->jitter_ms * 1000.0, order++, p2, 2});
                }
            }
            ++pkt_i;
            ++local_i;
            next_send = double(pkt_i) * period_us;
        }
    };

    const int B = c->render_block;
    std::vector<float> out(static_cast<size_t>(B), 0.f);
    std::vector<float> all;
    if (c->wav_path) all.reserve(size_t(c->seconds * 48000) + 1024);
    const double thr = 3.0 * c->amp * 2 * M_PI * c->freq / 48000.0;
    float prev = 0;
    bool have_prev = false;
    uint64_t under0 = 0, conc0 = 0;
    bool warm = false;
    double lost_since = -1;
    uint64_t blk = 0;
    for (;;) {
        double t = double(blk) * B * 1e6 / 48000.0;
        if (t > end_us) break;
        send_until(t);
        while (!net.empty() && net.top().t_us <= t) {
            const Arrival& a = net.top();
            slowik_engine_push_packet_from(e, a.pkt.data(), a.pkt.size(), uint64_t(a.t_us), a.key);
            net.pop();
        }
        if (t < c->render_start_ms * 1e3) {
            ++blk;
            continue;
        }
        slowik_engine_render(e, out.data(), B);
        SlowikStats st;
        slowik_engine_get_stats(e, &st);
        double ts = t / 1e6;
        if (!warm && ts >= c->warmup_s) {
            warm = true;
            under0 = st.underruns;
            conc0 = st.frames_concealed;
        }
        if (st.link_state == SLOWIK_LINK_LOST) {
            if (!r->saw_link_lost) {
                r->saw_link_lost = 1;
                r->link_lost_at_s = ts;
            }
            if (lost_since < 0) lost_since = ts;
            if (ts - lost_since > 0.01)
                for (float x : out) r->max_abs_while_lost = std::fmax(r->max_abs_while_lost, std::fabs(x));
        } else {
            if (lost_since >= 0 && st.link_state == SLOWIK_LINK_CONNECTED && r->recovered_at_s < 0)
                r->recovered_at_s = ts;
            lost_since = -1;
        }
        if (warm) {
            if (st.target_ms > r->target_max_ms) r->target_max_ms = st.target_ms;
            if (ts >= c->warmup_s + c->settle_s) {
                r->latency_max_ms = std::fmax(r->latency_max_ms, st.latency_ms);
                r->fill_err_max_ms = std::fmax(r->fill_err_max_ms, std::fabs(st.fill_ms - st.target_ms));
                r->fill_max_ms = std::fmax(r->fill_max_ms, st.fill_ms);
                r->fill_min_ms = std::fmin(r->fill_min_ms, st.fill_ms);
            }
            for (float x : out) {
                if (have_prev) {
                    double step = std::fabs(double(x) - prev);
                    if (step > r->max_step) r->max_step = step;
                    if (step > thr) ++r->discontinuities;
                }
                prev = x;
                have_prev = true;
            }
        }
        if (c->wav_path) all.insert(all.end(), out.begin(), out.end());
        ++blk;
    }
    slowik_engine_get_stats(e, &r->stats);
    r->underruns_after_warmup = r->stats.underruns - under0;
    r->concealed_after_warmup = r->stats.frames_concealed - conc0;
    r->drift_ppm_final = r->stats.drift_ppm;
    if (c->wav_path) write_wav(c->wav_path, all);
    slowik_engine_destroy(e);
    return 0;
}
