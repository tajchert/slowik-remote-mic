#include "engine.h"

#include <android/log.h>
#include <arpa/inet.h>
#include <cmath>
#include <cstring>
#include <netdb.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TAG "SlowikAudio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

namespace slowik {

static int64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static constexpr size_t kRingSamples = 32768;  // ~680 ms
static constexpr int kSampleRate = 48000;

Engine::Engine() : ring_(kRingSamples) {}
Engine::~Engine() { stop(); }

bool Engine::start(const std::string& host, int port, bool unprocessedSupported) {
    if (running_.load()) return true;
    host_ = host;
    port_ = port;
    unprocessed_ = unprocessedSupported;
    ring_.clear();
    framesWritten_.store(0);
    packetsSent_.store(0);
    lastControlNs_.store(0);
    overruns_.store(0);
    needReopen_.store(false);
    running_.store(true);
    if (!openStream()) {
        LOGW("initial stream open failed; sender thread will retry");
        needReopen_.store(true);
    }
    sender_ = std::thread([this] { senderLoop(); });
    return true;
}

void Engine::stop() {
    if (!running_.exchange(false)) return;
    if (sender_.joinable()) sender_.join();
    closeStream();
}

bool Engine::openStream() {
    oboe::AudioStreamBuilder b;
    b.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setSampleRate(kSampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setChannelCount(oboe::ChannelCount::Mono)
        ->setFormat(oboe::AudioFormat::Float)
        ->setInputPreset(unprocessed_ ? oboe::InputPreset::Unprocessed : oboe::InputPreset::VoicePerformance)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    // Engine outlives streams (stop() closes them before destruction); Oboe holds raw pointers here.
    std::shared_ptr<oboe::AudioStream> s;
    oboe::Result r = b.openStream(s);
    if (r != oboe::Result::OK) {
        LOGW("openStream failed: %s", oboe::convertToText(r));
        return false;
    }
    LOGI("granted: rate=%d sharing=%s preset=%d perf=%d framesPerBurst=%d framesPerCb=%d fmt=%d ch=%d",
         s->getSampleRate(), oboe::convertToText(s->getSharingMode()), static_cast<int>(s->getInputPreset()),
         static_cast<int>(s->getPerformanceMode()), s->getFramesPerBurst(), s->getFramesPerDataCallback(),
         static_cast<int>(s->getFormat()), s->getChannelCount());
    r = s->requestStart();
    if (r != oboe::Result::OK) {
        LOGW("requestStart failed: %s", oboe::convertToText(r));
        s->close();
        return false;
    }
    granted_[0] = s->getSampleRate();
    granted_[1] = static_cast<int>(s->getSharingMode());
    granted_[2] = static_cast<int>(s->getInputPreset());
    granted_[3] = s->getFramesPerBurst();
    granted_[4] = s->getFramesPerDataCallback();
    granted_[5] = static_cast<int>(s->getPerformanceMode());
    streamOpen_.store(true);
    stream_ = s;
    return true;
}

void Engine::closeStream() {
    streamOpen_.store(false);
    if (stream_) {
        stream_->stop();
        stream_->close();
        stream_.reset();
    }
}

oboe::DataCallbackResult Engine::onAudioReady(oboe::AudioStream*, void* data, int32_t numFrames) {
    const float* in = static_cast<const float*>(data);
    float pk = 0, sum = 0;
    for (int32_t i = 0; i < numFrames; i++) {
        float a = std::fabs(in[i]);
        if (a > pk) pk = a;
        sum += in[i] * in[i];
    }
    size_t w = ring_.write(in, static_cast<size_t>(numFrames));
    if (w < static_cast<size_t>(numFrames)) overruns_.fetch_add(1, std::memory_order_relaxed);
    // peak hold with decay; rms smoothed
    float oldPk = peak_.load(std::memory_order_relaxed);
    float decayed = oldPk * 0.95f;
    peak_.store(pk > decayed ? pk : decayed, std::memory_order_relaxed);
    float blockRms = numFrames > 0 ? std::sqrt(sum / numFrames) : 0.f;
    float oldRms = rms_.load(std::memory_order_relaxed);
    rms_.store(oldRms * 0.8f + blockRms * 0.2f, std::memory_order_relaxed);
    // capture clock: vDSO clock_gettime, no syscall. Written before frames count; reader retries.
    lastCbNs_.store(nowNs(), std::memory_order_relaxed);
    lastCbFrames_.store(framesWritten_.fetch_add(w, std::memory_order_release) + w, std::memory_order_release);
    return oboe::DataCallbackResult::Continue;
}

void Engine::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    LOGW("stream error after close: %s", oboe::convertToText(error));
    needReopen_.store(true);  // sender thread reopens
}

void Engine::pollControl(int fd) {
    uint8_t buf[256];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 0) return;
        Control c;
        if (decodeControl(buf, static_cast<size_t>(n), &c)) {
            live_.store(c.live);
            opMute_.store(c.operatorMute);
            loss_.store(c.lossPermille);
            latency_.store(c.latencyMs);
            jb_.store(c.jitterBufferMs);
            lastControlNs_.store(nowNs());
        }
    }
}

void Engine::senderLoop() {
    setpriority(PRIO_PROCESS, 0, -16);  // best effort
    int fd = -1;
    sockaddr_in addr{};
    bool haveAddr = false;
    int64_t lastTry = 0;

    uint32_t seq = 0;
    uint64_t consumed = 0;  // samples consumed from ring == sample_index of next frame
    int16_t cur[kFrameSamples] = {0};
    int16_t prev[kFrameSamples] = {0};
    float raw[kFrameSamples];
    uint8_t pkt[kAudioPacketSize];

    while (running_.load()) {
        const int64_t t = nowNs();
        if (!haveAddr && t - lastTry > 1000000000LL) {
            lastTry = t;
            addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            if (getaddrinfo(host_.c_str(), nullptr, &hints, &res) == 0 && res) {
                addr = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
                addr.sin_port = htons(static_cast<uint16_t>(port_));
                haveAddr = true;
                freeaddrinfo(res);
                fd = socket(AF_INET, SOCK_DGRAM, 0);
                if (fd >= 0 && connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
                    LOGW("connect failed");
                    close(fd);
                    fd = -1;
                    haveAddr = false;
                }
                int tos = 0xB8;  // EF
                if (fd >= 0) setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
            } else {
                LOGW("cannot resolve %s", host_.c_str());
            }
        }

        if (needReopen_.load() && t - lastTry > 500000000LL) {
            lastTry = t;
            closeStream();
            if (openStream()) needReopen_.store(false);
        }

        bool sentAny = false;
        while (ring_.available() >= kFrameSamples) {
            ring_.read(raw, kFrameSamples);
            const bool muted = muted_.load();
            std::memcpy(prev, cur, sizeof(cur));
            for (size_t i = 0; i < kFrameSamples; i++) cur[i] = muted ? 0 : floatToPcm16(raw[i]);

            // capture time of this frame's first sample, extrapolated from the last callback
            int64_t cbNs;
            uint64_t cbFrames;
            do {
                cbFrames = lastCbFrames_.load(std::memory_order_acquire);
                cbNs = lastCbNs_.load(std::memory_order_relaxed);
            } while (cbFrames != lastCbFrames_.load(std::memory_order_acquire));
            int64_t behind = static_cast<int64_t>(cbFrames) - static_cast<int64_t>(consumed);
            int64_t capNs = cbNs - behind * 1000000000LL / kSampleRate;
            if (capNs < 0) capNs = 0;

            AudioMeta m;
            m.seq = seq++;
            m.sampleIndex = consumed;
            m.captureTimeUs = static_cast<uint64_t>(capNs / 1000);
            m.battery = static_cast<uint8_t>(battery_.load());
            m.rssi = static_cast<int8_t>(rssi_.load());
            m.flags = (muted ? kFlagMuted : 0) | (charging_.load() ? kFlagCharging : 0);
            encodeAudio(pkt, m, cur, prev);
            consumed += kFrameSamples;
            if (fd >= 0 && send(fd, pkt, sizeof(pkt), MSG_DONTWAIT) == static_cast<ssize_t>(sizeof(pkt)))
                packetsSent_.fetch_add(1, std::memory_order_relaxed);
            sentAny = true;
        }
        if (fd >= 0) pollControl(fd);
        (void)sentAny;
        usleep(1000);
    }
    if (fd >= 0) close(fd);
}

EngineStats Engine::stats() const {
    EngineStats s{};
    s.peak = peak_.load();
    s.rms = rms_.load();
    s.packetsSent = packetsSent_.load();
    const int64_t lc = lastControlNs_.load();
    s.receiverConnected = lc != 0 && (nowNs() - lc) < 1000000000LL;
    s.live = live_.load();
    s.operatorMute = opMute_.load();
    s.lossPermille = loss_.load();
    s.latencyMs = latency_.load();
    s.jbMs = jb_.load();
    s.overruns = overruns_.load();
    s.streamOpen = streamOpen_.load() && !needReopen_.load();
    s.sampleRate = granted_[0].load();
    s.sharingMode = granted_[1].load();
    s.inputPreset = granted_[2].load();
    s.framesPerBurst = granted_[3].load();
    s.framesPerCallback = granted_[4].load();
    s.perfMode = granted_[5].load();
    return s;
}

}  // namespace slowik
