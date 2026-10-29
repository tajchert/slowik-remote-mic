// Slowik receiver engine. See macos/Receiver/CLAUDE.md for the design and threading rules.
//
// Data flow:
//   network thread: parse + validate -> PCM to float -> SPSC queue (per stream)
//   render thread:  drain queue -> jitter buffer ring (by frame index) -> frame reader with concealment
//                   -> windowed-sinc asynchronous resampler (ratio from PI controller on fill vs target)
//                   -> mute / link fade -> out
// All jitter-buffer state is owned by the render thread; cross-thread data is SPSC queue + relaxed atomics.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

#include "slowik_engine.h"

namespace slowik {

constexpr int kRate = SLOWIK_SAMPLE_RATE;
constexpr int kFrame = SLOWIK_FRAME_SAMPLES;
constexpr int kMaxFramesPerPacket = 4;
constexpr int kRingFrames = 128;  // 640 ms of storage
constexpr int kQueueCap = 128;    // 640 ms of packets
constexpr int kMaxStreams = 4;
constexpr int kJitBucket = 200;      // first-arrival transit samples per jitter bucket (~1 s)
constexpr int kJitBuckets = 10;      // buckets kept (~10 s); the worst one is ignored (single burst/stall)
constexpr int64_t kJitSkipAfterSync = kRate;  // ignore arrivals in the first 1 s after (re)sync (start burst)
constexpr double kDropExcessMs = 10;  // head-drop when fill stays > target + this ...
constexpr double kDropHoldMs = 500;   // ... for this long
constexpr int kXfade = 96;           // 2 ms crossfade concealment -> real audio
constexpr double kConcealTau = 0.015;
constexpr int64_t kRestartBackSamples = 9600;  // sample_index jumps back > 200 ms => sender restart
constexpr int kMeterWindow = 2400;             // 50 ms

inline int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
inline int ring_slot(int64_t f) { return int(((f % kRingFrames) + kRingFrames) % kRingFrames); }

// ---------------------------------------------------------------------------------------------
template <typename T, size_t N>
class SpscQueue {
public:
    T* begin_write() {
        size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= N) return nullptr;
        return &buf_[h % N];
    }
    void commit_write() { head_.store(head_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
    T* front() {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return nullptr;
        return &buf_[t % N];
    }
    void pop() { tail_.store(tail_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

private:
    T buf_[N];
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

struct Item {
    uint64_t arrival_us;
    uint64_t capture_us;
    int64_t newest_start;
    uint32_t seq;
    int nframes;
    float pcm[kMaxFramesPerPacket][kFrame];
};

// ---------------------------------------------------------------------------------------------
// 16-tap Kaiser-windowed sinc, 256 phases with linear interpolation between phases.
// ~8 samples (0.17 ms) of delay, 32 MACs per output sample.
class Resampler {
public:
    static constexpr int kTaps = 16;
    static constexpr int kPhases = 256;

    Resampler() {
        const double fc = 0.88;  // cutoff relative to Nyquist
        const double beta = 6.0;
        auto i0 = [](double x) {
            double s = 1, t = 1;
            for (int k = 1; k < 30; ++k) {
                t *= (x / (2 * k)) * (x / (2 * k));
                s += t;
            }
            return s;
        };
        const double i0b = i0(beta);
        for (int p = 0; p <= kPhases; ++p) {
            double f = double(p) / kPhases;
            double sum = 0;
            for (int k = 0; k < kTaps; ++k) {
                double t = f + 7 - k;  // distance from tap k (s[i-7+k]) to output position i+f
                double x = fc * t;
                double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
                double r = t / 8.0;
                double w = std::fabs(r) >= 1 ? 0.0 : i0(beta * std::sqrt(1 - r * r)) / i0b;
                table_[p * kTaps + k] = float(sinc * w);
                sum += sinc * w;
            }
            for (int k = 0; k < kTaps; ++k) table_[p * kTaps + k] = float(table_[p * kTaps + k] / sum);
        }
        reset();
    }
    void reset() {
        std::memset(hist_, 0, sizeof(hist_));
        pos_ = 0;
        frac_ = 0;
    }
    template <typename Pull>
    float next(double ratio, Pull&& pull) {
        double pf = frac_ * kPhases;
        int p = int(pf);
        float a = float(pf - p);
        const float* r0 = table_ + p * kTaps;
        const float* r1 = r0 + kTaps;
        const float* w = hist_ + pos_;
        float y = 0;
        for (int k = 0; k < kTaps; ++k) y += w[k] * (r0[k] + a * (r1[k] - r0[k]));
        frac_ += ratio;
        while (frac_ >= 1.0) {
            frac_ -= 1.0;
            float x = pull();
            hist_[pos_] = x;
            hist_[pos_ + kTaps] = x;
            pos_ = (pos_ + 1) % kTaps;
        }
        return y;
    }

private:
    float table_[(kPhases + 1) * kTaps];
    float hist_[2 * kTaps];
    int pos_ = 0;
    double frac_ = 0;
};

// ---------------------------------------------------------------------------------------------
struct RenderStats {
    std::atomic<uint64_t> frames_played{0}, frames_concealed{0}, frames_late{0}, frames_duplicate{0}, frames_dropped{0},
        underruns{0}, resyncs{0}, link_lost_events{0};
    std::atomic<double> loss_permille{0}, jitter_ms{0}, jitter_mean_ms{0}, target_ms{0}, fill_ms{0},
        drift_ppm{0}, ratio_ppm{0}, latency_ms{0};
    std::atomic<float> peak{0}, rms{0};
    std::atomic<int> link_state{SLOWIK_LINK_WAITING}, render_block{0};
};

// Render-thread-owned per-stream jitter buffer + reader + resampler.
class Pipeline {
public:
    void init(const SlowikConfig& c) {
        cfg_ = c;
        target_ = c.initial_target_ms * kRate / 1000.0;
        for (auto& t : tag_) t = INT64_MIN;
        decay_ = float(std::exp(-1.0 / (kConcealTau * kRate)));
    }

    void process(const Item& it, RenderStats& st) {
        int64_t newest = it.newest_start;
        bool misaligned = synced_ && floor_div(newest - align_, kFrame) * kFrame != newest - align_;
        // Sender restart: sample_index jumps back while the sender's capture clock keeps going forward. A packet
        // that is merely old (reordered inside a burst) has an older capture time and is just late.
        bool back = newest + kRestartBackSamples < max_start_ && it.capture_us + 50000 >= max_capture_;
        bool restart = synced_ && (back || misaligned);
        bool too_far = synced_ && (newest + kFrame - rd_ > int64_t(kRingFrames - 4) * kFrame);
        if (!synced_ || link_state_ == SLOWIK_LINK_LOST || restart || too_far) {
            resync(it, st);
        }
        // Duplicate packet (same seq seen recently). Reordering is not a duplicate.
        {
            SeqSeen& ss = seen_[it.seq % kSeqWindow];
            if (ss.valid && ss.seq == it.seq) {
                ++dups_;
                st.frames_duplicate.store(dups_, std::memory_order_relaxed);
                return;
            }
            ss.valid = true;
            ss.seq = it.seq;
        }
        if (newest > max_start_) max_start_ = newest;
        if (it.capture_us > max_capture_) max_capture_ = it.capture_us;
        for (int k = 0; k < it.nframes; ++k) {
            int64_t start = newest - int64_t(k) * kFrame;
            if (start < 0) break;
            insert(start, it.pcm[k], it.arrival_us);
        }
    }

    void render(float* out, int n, bool muted, double out_latency_ms, RenderStats& st) {
        if (n > block_) block_ = n;
        if (!synced_) {
            std::memset(out, 0, sizeof(float) * size_t(n));
            publish(st, out, n, out_latency_ms);
            return;
        }
        stalled_ = false;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            float y = rs_.next(ratio_, [this]() { return next_input(); });
            float tgt = (muted || link_state_ == SLOWIK_LINK_LOST) ? 0.f : 1.f;
            if (gain_ < tgt)
                gain_ = std::min(tgt, gain_ + 1.f / kFrame);
            else if (gain_ > tgt)
                gain_ = std::max(tgt, gain_ - 1.f / kFrame);
            out[i] = y * gain_;
            peak = std::max(peak, std::fabs(out[i]));
        }
        (void)peak;
        samples_since_new_ += n;
        if (link_state_ == SLOWIK_LINK_CONNECTED &&
            samples_since_new_ > int64_t(cfg_.link_lost_ms * kRate / 1000.0)) {
            link_state_ = SLOWIK_LINK_LOST;
            ++link_lost_events_;
        }
        control(n);
        // Loss buckets (1 s each, 5 buckets).
        bucket_samples_ += n;
        if (bucket_samples_ >= kRate) {
            bucket_samples_ -= kRate;
            bucket_ = (bucket_ + 1) % 5;
            played_b_[bucket_] = lost_b_[bucket_] = 0;
        }
        publish(st, out, n, out_latency_ms);
    }

    int link_state() const { return link_state_; }

private:
    enum Mode { kGood, kConceal, kStall };

    int64_t frame_start(int64_t f) const { return align_ + f * kFrame; }

    void resync(const Item& it, RenderStats& st) {
        align_ = ((it.newest_start % kFrame) + kFrame) % kFrame;
        for (auto& t : tag_) t = INT64_MIN;
        max_end_ = it.newest_start + kFrame;
        max_start_ = it.newest_start;
        max_capture_ = it.capture_us;
        rd_ = max_end_ - int64_t(std::lround(target_));
        int64_t oldest = it.newest_start - int64_t(it.nframes - 1) * kFrame;
        if (oldest < 0) oldest = align_;
        first_frame_ = floor_div(oldest - align_, kFrame);
        cur_f_ = INT64_MIN;
        mode_ = kConceal;
        // Start "concealing" from silence so the first real frame fades in over kXfade samples.
        std::memset(hist_, 0, sizeof(hist_));
        std::memset(conc_, 0, sizeof(conc_));
        concealing_ = true;
        conc_gain_ = 0;
        conc_idx_ = 0;
        conc_dir_ = 1;
        xfade_left_ = 0;
        rs_.reset();
        fill_s_ = target_;
        bk_n_ = bres_count_ = bres_pos_ = 0;
        dcount_ = dpos_ = 0;  // sender restart changes the transit offset; keep the last estimate meanwhile
        since_sync_ = 0;
        over_ = 0;
        drop_req_ = 0;
        reanchor_ = true;  // after this drain, anchor on the newest data (a burst may follow in the queue)
        for (auto& ss : seen_) ss.valid = false;
        for (auto& v : raw_hist_) v = 0;
        shrinking_ = false;
        ff_ = 0;
        catchup_left_ = kRate * 10;  // allow fast convergence for up to 10 s after a resync
        samples_since_new_ = 0;
        link_state_ = SLOWIK_LINK_CONNECTED;
        synced_ = true;
        ++resyncs_;
        st.resyncs.store(resyncs_, std::memory_order_relaxed);
    }

    void insert(int64_t start, const float* pcm, uint64_t arrival_us) {
        int64_t rel = start - align_;
        int64_t f = floor_div(rel, kFrame);
        if (f * kFrame != rel) return;  // misaligned older frame; ignore
        int64_t end = start + kFrame;
        int s = ring_slot(f);
        if (tag_[s] == f) return;  // already have it (redundant copy)
        if (end <= rd_ || (f == cur_f_ && mode_ != kStall)) {
            // Too late: its playout time has passed (or it is being concealed right now).
            if (f >= first_frame_) ++late_;
            return;
        }
        tag_[s] = f;
        std::memcpy(data_[s], pcm, sizeof(float) * kFrame);
        if (end > max_end_) max_end_ = end;
        samples_since_new_ = 0;
        // First-arrival transit (sender sample clock vs arrival clock) for the jitter estimate.
        double transit = double(arrival_us) - double(end) * 1e6 / kRate;
        if (since_sync_ < kJitSkipAfterSync) return;  // start-up burst would poison the estimate
        bk_[bk_n_++] = transit;
        if (bk_n_ == kJitBucket) {
            bk_n_ = 0;
            int k50 = kJitBucket / 2, k99 = kJitBucket - 1;  // max of the bucket (any late frame counts)
            double mn = *std::min_element(bk_, bk_ + kJitBucket);
            std::nth_element(bk_, bk_ + k99, bk_ + kJitBucket);
            double p99 = bk_[k99];
            std::nth_element(bk_, bk_ + k50, bk_ + k99);
            double p50 = bk_[k50];
            bres_p_[bres_pos_] = (p99 - p50) / 1000.0;
            bres_m_[bres_pos_] = (p50 - mn) / 1000.0;
            bres_pos_ = (bres_pos_ + 1) % kJitBuckets;
            if (bres_count_ < kJitBuckets) ++bres_count_;
            dmin_[dpos_] = mn;
            dtime_[dpos_] = double(arrival_us);
            dpos_ = (dpos_ + 1) % kDriftPts;
            if (dcount_ < kDriftPts) ++dcount_;
            if (dcount_ >= 5) {
                double mt = 0, mm = 0;
                for (int i = 0; i < dcount_; ++i) { mt += dtime_[i]; mm += dmin_[i]; }
                mt /= dcount_;
                mm /= dcount_;
                double num = 0, den = 0;
                for (int i = 0; i < dcount_; ++i) {
                    num += (dtime_[i] - mt) * (dmin_[i] - mm);
                    den += (dtime_[i] - mt) * (dtime_[i] - mt);
                }
                // transit grows when the phone's sample clock is slower than ours: drift = -slope.
                if (den > 0) drift_s_ = -num / den;
            }
        }
    }

public:
    // Called after each queue drain: right after a resync, re-anchor the read position on the newest data so a
    // burst of queued packets (Wi-Fi power-save wake-up) does not start the buffer hundreds of ms deep.
    void after_drain() {
        if (!reanchor_) return;
        reanchor_ = false;
        int64_t want = max_end_ - int64_t(std::lround(target_));
        if (want > rd_) rd_ = want;
        fill_s_ = target_;
    }

private:

    float conceal_next() {
        if (!concealing_) {
            for (int i = 0; i < kFrame; ++i) conc_[i] = hist_[(hist_pos_ + i) % kFrame];
            conc_idx_ = kFrame - 2;  // continue backwards from the last good sample (mirror)
            conc_dir_ = -1;
            conc_gain_ = 1;
            concealing_ = true;
        }
        float v = conc_[conc_idx_] * conc_gain_;
        conc_gain_ *= decay_;
        conc_idx_ += conc_dir_;
        if (conc_idx_ < 0) {
            conc_idx_ = 1;
            conc_dir_ = 1;
        } else if (conc_idx_ >= kFrame) {
            conc_idx_ = kFrame - 2;
            conc_dir_ = -1;
        }
        return v;
    }

    void enter_frame(int64_t f) {
        cur_f_ = f;
        bool counted = f >= first_frame_;
        if (counted) {
            ++played_;
            ++played_b_[bucket_];
        }
        if (tag_[ring_slot(f)] == f) {
            mode_ = kGood;
            if (concealing_ && xfade_left_ == 0) xfade_left_ = kXfade;
        } else if (max_end_ > frame_start(f) + kFrame) {
            mode_ = kConceal;
            if (counted) {
                ++concealed_;
                ++lost_b_[bucket_];
            }
        } else {
            mode_ = kStall;  // classified when it resolves: late data (underrun) or loss (concealment)
        }
    }

    float next_input() {
        if (drop_req_ > 0 && mode_ != kStall && xfade_left_ == 0) {
            // Head-drop excess latency: skip whole frames. While playing, crossfade from the mirror continuation
            // of the last 5 ms into the new position; while concealing, the next good frame crossfades anyway.
            rd_ += drop_req_ * kFrame;
            fill_s_ -= double(drop_req_ * kFrame);
            dropped_ += uint64_t(drop_req_);
            drop_req_ = 0;
            if (!concealing_) xfade_left_ = kXfade;  // conceal_next() snapshots the last 5 ms played
        }
        int64_t f = floor_div(rd_ - align_, kFrame);
        if (f != cur_f_) enter_frame(f);
        for (;;) {
            if (mode_ == kStall) {
                if (tag_[ring_slot(f)] == f) {
                    // The frame was late, not lost: a real underrun. Grow fast: +2 ms on top of the stall.
                    if (f >= first_frame_ && link_state_ == SLOWIK_LINK_CONNECTED) {
                        ++underruns_;
                        target_ = std::min(target_ + 2.0 * kRate / 1000.0, cfg_.max_target_ms * kRate / 1000.0);
                    }
                    mode_ = kGood;
                    if (concealing_ && xfade_left_ == 0) xfade_left_ = kXfade;
                    continue;
                }
                if (max_end_ > frame_start(f) + kFrame) {
                    mode_ = kConceal;
                    if (f >= first_frame_) {
                        ++concealed_;
                        ++lost_b_[bucket_];
                    }
                    continue;
                }
                stalled_ = true;
                return conceal_next();  // do not advance: the buffer grows by the stalled time
            }
            if (mode_ == kConceal) {
                ++rd_;
                return conceal_next();
            }
            float g = data_[ring_slot(f)][rd_ - frame_start(f)];
            if (xfade_left_ > 0) {
                float a = 1.f - float(xfade_left_) / kXfade;
                g = a * g + (1.f - a) * conceal_next();
                if (--xfade_left_ == 0) concealing_ = false;
            }
            hist_[hist_pos_] = g;
            hist_pos_ = (hist_pos_ + 1) % kFrame;
            ++rd_;
            return g;
        }
    }

    void control(int n) {
        const double dt = double(n) / kRate;
        const double fill = double(max_end_ - rd_);
        fill_s_ += (fill - fill_s_) * (1.0 - std::exp(-dt / 0.3));

        // Excess fill (burst after a stall, or anything the resampler can't fix quickly): if the fill stays more
        // than kDropExcessMs above target for kDropHoldMs, drop whole frames down to the target.
        since_sync_ += n;
        if (link_state_ == SLOWIK_LINK_CONNECTED && fill > target_ + kDropExcessMs * kRate / 1000.0) {
            over_min_ = over_ == 0 ? fill : std::min(over_min_, fill);
            over_ += n;
            if (over_ >= int64_t(kDropHoldMs * kRate / 1000.0)) {
                drop_req_ = int64_t((over_min_ - target_) / kFrame);
                over_ = 0;
            }
        } else {
            over_ = 0;
        }

        target_tick_ += n;
        if (target_tick_ >= kRate / 5) {
            target_tick_ = 0;
            update_target();
        }
        if (link_state_ != SLOWIK_LINK_CONNECTED || stalled_) return;  // freeze the PI loop
        const double maxr = cfg_.max_ppm * 1e-6;
        double e = (fill_s_ - target_) / kRate;  // seconds; >0 = too much buffered -> consume faster
        double lim = maxr + ff_, kp = cfg_.kp;
        bool catchup = catchup_left_ > 0;
        if (catchup) {
            // Right after a resync: faster convergence onto the (still settling) target, up to 0.2 %.
            catchup_left_ -= n;
            lim = std::max(maxr, 2000e-6);
            kp = 5 * cfg_.kp;
        }
        // The integrator learns the clock drift only: no integration on large errors (target step, stall)
        // or while the output is saturated in the direction of the error (anti-windup).
        double u = kp * e + integ_ + ff_;
        bool sat = (u >= lim && e > 0) || (u <= -lim && e < 0);
        if (std::fabs(e) < 0.002 && !sat) integ_ = std::clamp(integ_ + cfg_.ki * e * dt, -maxr, maxr);
        double r = std::clamp(kp * e + integ_ + ff_, -lim, lim);
        ratio_ = 1.0 + r;
    }

    void update_target() {
        if (bres_count_ == 0) return;
        // Robust jitter: 2nd-worst of the last ~10 one-second buckets (max - p50 each), so one burst or stall
        // (a flush lands in one bucket) does not set the target; jitter seen in two seconds of ten does.
        double v[kJitBuckets], m[kJitBuckets];
        int n = bres_count_;
        std::memcpy(v, bres_p_, sizeof(double) * size_t(n));
        std::memcpy(m, bres_m_, sizeof(double) * size_t(n));
        std::sort(v, v + n, [](double x, double y) { return x > y; });
        std::sort(m, m + n);
        jitter_ms_ = v[std::min(1, n - 1)];
        jitter_mean_ms_ = m[n / 2];
        // Fill is measured right after a render call has consumed its block, so the block size itself cancels
        // out of the requirement; only drain granularity matters. Cap its contribution at 128 frames' worth so
        // 512/1024-frame callbacks don't inflate the target (they used to add 2 x 21 ms).
        double block_ms = 1000.0 * std::min(block_, 128) / kRate;
        // half a packet of sawtooth + one block of drain granularity + one block of render-ahead + resampler
        double raw_ms = jitter_ms_ + 2.5 + 2 * block_ms + 8.0 * 1000.0 / kRate + cfg_.margin_ms;
        raw_ms = std::clamp(raw_ms, cfg_.min_target_ms, cfg_.max_target_ms);
        double raw = raw_ms * kRate / 1000.0;
        // Peak-hold over the last 10 s: percentiles near a cluster edge (e.g. 1-2 % of frames arriving one
        // packet late via redundancy) flip between modes; shrinking only below the held peak keeps it stable.
        raw_hist_[raw_pos_] = raw;
        raw_pos_ = (raw_pos_ + 1) % kRawHold;
        double held = raw;
        for (double v : raw_hist_) held = std::max(held, v);
        ff_ = 0;
        if (raw > target_ || catchup_left_ > 0) {
            target_ = raw;  // grow fast (and settle immediately right after a resync)
        } else if (held < target_ - 2.0 * kRate / 1000.0 || (shrinking_ && held < target_)) {
            // Hysteresis: start shrinking only when the target is > 2 ms too big, then go all the way.
            shrinking_ = true;
            // Shrink slowly by playing slightly fast: 100 ppm normally, 250 ppm in low-level passages
            // (< -40 dBFS), with the rate fed forward so the drift integrator does not absorb it.
            double ppm = level_rms_ < 0.01f ? 250.0 : 100.0;
            double step = std::min(target_ - held, ppm * 1e-6 * kRate * 0.2);
            target_ -= step;
            ff_ = step / (0.2 * kRate);
        } else {
            shrinking_ = false;
        }
    }

    void publish(RenderStats& st, const float* out, int n, double out_latency_ms) {
        for (int i = 0; i < n; ++i) {
            float a = std::fabs(out[i]);
            if (a > m_peak_) m_peak_ = a;
            m_sum_ += double(out[i]) * out[i];
        }
        m_count_ += n;
        if (m_count_ >= kMeterWindow) {
            st.peak.store(m_peak_, std::memory_order_relaxed);
            level_rms_ = float(std::sqrt(m_sum_ / m_count_));
            st.rms.store(level_rms_, std::memory_order_relaxed);
            m_peak_ = 0;
            m_sum_ = 0;
            m_count_ = 0;
        }
        auto R = std::memory_order_relaxed;
        st.frames_played.store(played_, R);
        st.frames_concealed.store(concealed_, R);
        st.frames_late.store(late_, R);
        st.underruns.store(underruns_, R);
        st.frames_dropped.store(dropped_, R);
        st.link_lost_events.store(link_lost_events_, R);
        uint64_t p = 0, l = 0;
        for (int i = 0; i < 5; ++i) {
            p += played_b_[i];
            l += lost_b_[i];
        }
        st.loss_permille.store(p ? 1000.0 * double(l) / double(p) : 0.0, R);
        st.jitter_ms.store(jitter_ms_, R);
        st.jitter_mean_ms.store(jitter_mean_ms_, R);
        st.target_ms.store(target_ * 1000.0 / kRate, R);
        double fill_ms = fill_s_ * 1000.0 / kRate;
        st.fill_ms.store(fill_ms, R);
        st.drift_ppm.store(drift_s_ * 1e6, R);
        st.ratio_ppm.store((ratio_ - 1.0) * 1e6, R);
        // capture framing (5) + network (~1, unmeasurable without clock sync) + mean jitter + half packet
        // + buffer fill + resampler delay + output device latency.
        double lat = synced_ ? 5.0 + 1.0 + jitter_mean_ms_ + 2.5 + fill_ms + 8.0 * 1000.0 / kRate + out_latency_ms
                             : 0.0;
        st.latency_ms.store(lat, R);
        st.link_state.store(link_state_, R);
        st.render_block.store(block_, R);
    }

    SlowikConfig cfg_{};
    // ring
    int64_t tag_[kRingFrames];
    float data_[kRingFrames][kFrame];
    bool synced_ = false;
    int64_t align_ = 0, rd_ = 0, max_end_ = 0, max_start_ = 0, first_frame_ = 0, cur_f_ = INT64_MIN;
    Mode mode_ = kConceal;
    // concealment
    float hist_[kFrame] = {};
    int hist_pos_ = 0;
    float conc_[kFrame] = {};
    int conc_idx_ = 0, conc_dir_ = 1;
    float conc_gain_ = 0, decay_ = 1;
    bool concealing_ = false;
    int xfade_left_ = 0;
    // resampler + control
    Resampler rs_;
    double ratio_ = 1.0, integ_ = 0, fill_s_ = 0, target_ = 0, ff_ = 0, drift_s_ = 0;
    float level_rms_ = 0;
    bool shrinking_ = false;
    static constexpr int kRawHold = 50;  // 10 s of 200 ms target updates
    double raw_hist_[kRawHold] = {};
    int raw_pos_ = 0;
    int target_tick_ = 0, block_ = 0;
    bool stalled_ = false;
    float gain_ = 0;
    int link_state_ = SLOWIK_LINK_WAITING;
    int64_t samples_since_new_ = 0;
    // jitter
    double bk_[kJitBucket];
    int bk_n_ = 0;
    double bres_p_[kJitBuckets] = {}, bres_m_[kJitBuckets] = {};
    int bres_count_ = 0, bres_pos_ = 0;
    int64_t since_sync_ = 0, over_ = 0, drop_req_ = 0;
    double over_min_ = 0;
    bool reanchor_ = false;
    // Drift: least-squares slope of the per-second MINIMUM transit (lower envelope of arrival time vs sender
    // sample clock) over the last 30 s. The envelope is ~0.1-1 ms clean even with 10+ ms jitter, unlike the fill.
    static constexpr int kDriftPts = 30;
    double dmin_[kDriftPts] = {}, dtime_[kDriftPts] = {};
    int dcount_ = 0, dpos_ = 0;
    uint64_t max_capture_ = 0;
    uint64_t dropped_ = 0;
    struct SeqSeen {
        uint32_t seq;
        bool valid;
    };
    static constexpr int kSeqWindow = 512;
    SeqSeen seen_[kSeqWindow] = {};
    int64_t catchup_left_ = 0;
    double jitter_ms_ = 0, jitter_mean_ms_ = 0;
    // counters
    uint64_t played_ = 0, concealed_ = 0, late_ = 0, dups_ = 0, underruns_ = 0, resyncs_ = 0, link_lost_events_ = 0;
    uint64_t played_b_[5] = {}, lost_b_[5] = {};
    int bucket_ = 0, bucket_samples_ = 0;
    // meter
    float m_peak_ = 0;
    double m_sum_ = 0;
    int m_count_ = 0;
};

// ---------------------------------------------------------------------------------------------
struct Stream {
    // network-thread owned
    bool have_seq = false;
    uint32_t last_seq = 0;     // highest seq seen (wrap-aware)
    int64_t ext_highest = 0;   // extended (unwrapped) highest seq relative to base
    uint64_t since_base = 0;   // packets received since base
    uint64_t lost_run = 0, lost_total_prev = 0;
    bool have_sender = false;
    uint64_t sender_key = 0, sender_last_us = 0;
    std::atomic<uint64_t> foreign{0};
    // network -> anyone
    std::atomic<int> id{-1};
    std::atomic<uint64_t> received{0}, invalid{0}, dropped{0}, lost_seq{0};
    std::atomic<int> battery{-1}, rssi{0}, flags{0}, codec{0};
    SpscQueue<Item, kQueueCap> queue;
    // render-thread owned
    Pipeline pipe;
};

}  // namespace slowik

using namespace slowik;

struct SlowikEngine {
    SlowikConfig cfg;
    Stream streams[kMaxStreams];
    std::atomic<int> stream_count{0};
    std::atomic<uint64_t> invalid{0};
    std::atomic<int> live{0}, mute{0};
    std::atomic<double> out_latency_ms{0};
    std::atomic<uint32_t> ctl_seq{0};
    RenderStats rstats;
};

extern "C" void slowik_config_default(SlowikConfig* c) {
    c->min_target_ms = 10;
    c->max_target_ms = 80;
    c->initial_target_ms = 20;
    c->margin_ms = 3;
    c->max_ppm = 200;
    c->link_lost_ms = 150;
    // PI on fill error (seconds): critically damped (wn = 0.05 rad/s); 1 ms error -> 100 ppm.
    c->kp = 0.1;
    c->ki = 0.0025;
}

extern "C" SlowikEngine* slowik_engine_create(const SlowikConfig* cfg) {
    auto* e = new (std::nothrow) SlowikEngine();
    if (!e) return nullptr;
    if (cfg)
        e->cfg = *cfg;
    else
        slowik_config_default(&e->cfg);
    for (auto& s : e->streams) s.pipe.init(e->cfg);
    return e;
}

extern "C" void slowik_engine_destroy(SlowikEngine* e) { delete e; }

extern "C" int slowik_engine_push_packet(SlowikEngine* e, const uint8_t* data, size_t len, uint64_t arrival_us) {
    return slowik_engine_push_packet_from(e, data, len, arrival_us, 0);
}

extern "C" int slowik_engine_push_packet_from(SlowikEngine* e, const uint8_t* data, size_t len, uint64_t arrival_us,
                                              uint64_t source_key) {
    SlowikAudioHeader h;
    int r = slowik_parse_audio(data, len, &h);
    if (r != SLOWIK_PARSE_OK) {
        e->invalid.fetch_add(1, std::memory_order_relaxed);
        return r;
    }
    int n = e->stream_count.load(std::memory_order_relaxed);
    Stream* s = nullptr;
    for (int i = 0; i < n; ++i)
        if (e->streams[i].id.load(std::memory_order_relaxed) == int(h.stream_id)) s = &e->streams[i];
    if (!s) {
        if (n >= kMaxStreams) {
            e->invalid.fetch_add(1, std::memory_order_relaxed);
            return SLOWIK_PUSH_TOO_MANY_STREAMS;
        }
        s = &e->streams[n];
        s->id.store(h.stream_id, std::memory_order_relaxed);
        e->stream_count.store(n + 1, std::memory_order_release);
    }
    // Sender lock: one source address per stream; switch only after the locked one is silent for > 2 s.
    if (s->have_sender && source_key != s->sender_key && arrival_us < s->sender_last_us + 2000000) {
        s->foreign.fetch_add(1, std::memory_order_relaxed);
        return SLOWIK_PUSH_FOREIGN_SENDER;
    }
    s->have_sender = true;
    s->sender_key = source_key;
    s->sender_last_us = arrival_us;
    s->received.fetch_add(1, std::memory_order_relaxed);
    // RTP-style loss: expected (highest - base + 1) minus received; reordering is not loss.
    int32_t d = int32_t(h.seq - s->last_seq);  // wrap-safe
    if (!s->have_seq || d < -1000 || d > 100000) {
        // first packet or sender restart: fold the previous run into the total and start a new base
        if (s->have_seq) s->lost_total_prev += s->lost_run;
        s->have_seq = true;
        s->last_seq = h.seq;
        s->ext_highest = 0;
        s->since_base = 0;
        s->lost_run = 0;
        d = 0;
    }
    if (d > 0) {
        s->ext_highest += d;
        s->last_seq = h.seq;
    }
    ++s->since_base;
    int64_t lost = s->ext_highest + 1 - int64_t(s->since_base);
    s->lost_run = lost > 0 ? uint64_t(lost) : 0;
    s->lost_seq.store(s->lost_total_prev + s->lost_run, std::memory_order_relaxed);
    s->battery.store(h.battery == 255 ? -1 : h.battery, std::memory_order_relaxed);
    s->rssi.store(h.rssi, std::memory_order_relaxed);
    s->flags.store(h.flags, std::memory_order_relaxed);
    s->codec.store(h.codec, std::memory_order_relaxed);

    Item* it = s->queue.begin_write();
    if (!it) {
        s->dropped.fetch_add(1, std::memory_order_relaxed);
        return SLOWIK_PUSH_QUEUE_FULL;
    }
    it->arrival_us = arrival_us;
    it->newest_start = int64_t(h.sample_index);
    it->seq = h.seq;
    it->capture_us = h.capture_time_us;
    it->nframes = std::min(h.frame_count, kMaxFramesPerPacket);
    const uint8_t* p = h.frames;
    const size_t bps = h.codec == 2 ? 3 : 2;
    for (int k = 0; k < it->nframes; ++k) {
        float* dst = it->pcm[k];
        const uint8_t* src = p + size_t(k) * kFrame * bps;
        if (bps == 2) {
            for (int i = 0; i < kFrame; ++i)
                dst[i] = float(int16_t(uint16_t(src[2 * i] | (src[2 * i + 1] << 8)))) * (1.0f / 32768.0f);
        } else {
            for (int i = 0; i < kFrame; ++i) {
                int32_t v = int32_t(uint32_t(src[3 * i]) << 8 | uint32_t(src[3 * i + 1]) << 16 |
                                    uint32_t(src[3 * i + 2]) << 24) >> 8;
                dst[i] = float(v) * (1.0f / 8388608.0f);
            }
        }
    }
    s->queue.commit_write();
    return 0;
}

extern "C" void slowik_engine_render(SlowikEngine* e, float* out, int n) {
    int count = e->stream_count.load(std::memory_order_acquire);
    // v1: the first stream seen is the one on air; others are drained and discarded.
    for (int i = 0; i < count; ++i) {
        Stream& s = e->streams[i];
        while (Item* it = s.queue.front()) {
            if (i == 0) s.pipe.process(*it, e->rstats);
            s.queue.pop();
        }
        if (i == 0) s.pipe.after_drain();
    }
    double lat = e->out_latency_ms.load(std::memory_order_relaxed);
    bool muted = e->mute.load(std::memory_order_relaxed) != 0;
    if (count > 0) {
        e->streams[0].pipe.render(out, n, muted, lat, e->rstats);
    } else {
        std::memset(out, 0, sizeof(float) * size_t(n));
    }
}

extern "C" void slowik_engine_get_stats(SlowikEngine* e, SlowikStats* o) {
    auto R = std::memory_order_relaxed;
    std::memset(o, 0, sizeof(*o));
    int count = e->stream_count.load(std::memory_order_acquire);
    o->streams_seen = count;
    o->stream_id = -1;
    o->battery = -1;
    o->packets_invalid = e->invalid.load(R);
    if (count > 0) {
        Stream& s = e->streams[0];
        o->stream_id = s.id.load(R);
        o->packets_received = s.received.load(R);
        o->packets_dropped_queue = s.dropped.load(R);
        o->packets_lost_seq = s.lost_seq.load(R);
        o->packets_foreign = s.foreign.load(R);
        o->battery = s.battery.load(R);
        o->rssi = s.rssi.load(R);
        o->flags = s.flags.load(R);
        o->codec = s.codec.load(R);
    }
    const RenderStats& r = e->rstats;
    o->frames_played = r.frames_played.load(R);
    o->frames_concealed = r.frames_concealed.load(R);
    o->frames_late = r.frames_late.load(R);
    o->frames_duplicate = r.frames_duplicate.load(R);
    o->underruns = r.underruns.load(R);
    o->frames_dropped = r.frames_dropped.load(R);
    o->resyncs = r.resyncs.load(R);
    o->link_lost_events = r.link_lost_events.load(R);
    o->loss_permille = r.loss_permille.load(R);
    o->jitter_ms = r.jitter_ms.load(R);
    o->jitter_mean_ms = r.jitter_mean_ms.load(R);
    o->target_ms = r.target_ms.load(R);
    o->fill_ms = r.fill_ms.load(R);
    o->drift_ppm = r.drift_ppm.load(R);
    o->ratio_ppm = r.ratio_ppm.load(R);
    o->latency_ms = r.latency_ms.load(R);
    o->peak = r.peak.load(R);
    o->rms = r.rms.load(R);
    o->link_state = r.link_state.load(R);
    o->render_block = r.render_block.load(R);
}

extern "C" void slowik_engine_set_live(SlowikEngine* e, int live) { e->live.store(live ? 1 : 0); }
extern "C" void slowik_engine_set_mute(SlowikEngine* e, int mute) { e->mute.store(mute ? 1 : 0); }
extern "C" void slowik_engine_set_output_latency_ms(SlowikEngine* e, double ms) { e->out_latency_ms.store(ms); }

extern "C" size_t slowik_engine_build_control(SlowikEngine* e, uint8_t* out, size_t cap) {
    SlowikStats s;
    slowik_engine_get_stats(e, &s);
    auto u16 = [](double v) { return uint16_t(std::clamp(std::lround(v), 0L, 65535L)); };
    return slowik_build_control(out, cap, uint16_t(s.stream_id < 0 ? 0 : s.stream_id),
                                e->ctl_seq.fetch_add(1, std::memory_order_relaxed), e->live.load(), e->mute.load(),
                                u16(s.loss_permille), u16(s.latency_ms), u16(s.target_ms));
}
