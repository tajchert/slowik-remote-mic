#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <oboe/Oboe.h>

#include "slowik_protocol.h"
#include "spsc_ring.h"

namespace slowik {

struct EngineStats {
    float peak, rms;
    uint64_t packetsSent;
    bool receiverConnected;
    int live, operatorMute, lossPermille, latencyMs, jbMs;
    int sampleRate, sharingMode, inputPreset, framesPerBurst, framesPerCallback, perfMode;
    bool streamOpen;
    uint64_t overruns;
};

class Engine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    Engine();
    ~Engine();

    bool start(const std::string& host, int port, bool unprocessedSupported);
    void stop();
    void setMuted(bool m) { muted_.store(m); }
    void setDeviceStatus(int battery, int rssi, bool charging) {
        battery_.store(battery);
        rssi_.store(rssi);
        charging_.store(charging);
    }
    EngineStats stats() const;

    // Oboe callbacks
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* s, oboe::Result error) override;

private:
    bool openStream();
    void closeStream();
    void senderLoop();
    void pollControl(int fd);

    std::string host_;
    int port_ = 0;
    bool unprocessed_ = false;

    std::shared_ptr<oboe::AudioStream> stream_;
    SpscRing ring_;
    std::thread sender_;
    std::atomic<bool> running_{false};
    std::atomic<bool> needReopen_{false};
    std::atomic<bool> streamOpen_{false};
    std::atomic<int> granted_[6]{};

    std::atomic<bool> muted_{false};
    std::atomic<int> battery_{255}, rssi_{0};
    std::atomic<bool> charging_{false};

    // meter + capture clock (written by audio callback only)
    std::atomic<float> peak_{0}, rms_{0};
    std::atomic<uint64_t> framesWritten_{0};  // total samples pushed to ring (incl. dropped = counted)
    std::atomic<int64_t> lastCbNs_{0};
    std::atomic<uint64_t> lastCbFrames_{0};
    std::atomic<uint64_t> overruns_{0};

    std::atomic<uint64_t> packetsSent_{0};
    std::atomic<int64_t> lastControlNs_{0};
    std::atomic<int> live_{0}, opMute_{0}, loss_{0}, latency_{0}, jb_{0};
};

}  // namespace slowik
