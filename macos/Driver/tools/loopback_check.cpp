// Plays a 1 kHz tone into "Slowik Mic" (output) while recording from its input
// for 2 s; prints detected level and estimated loopback latency.
// Build+run: tools/loopback_check.sh
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

static const double kRate = 48000, kFreq = 1000;
static const size_t kFrames = 48000 * 2;
static std::vector<float> gRec(kFrames * 2, 0.f);
static std::atomic<size_t> gRecPos { 0 };
static std::atomic<size_t> gPlayPos { 0 };
static std::atomic<long> gToneStartRec { -1 };

static OSStatus ioProc(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* in,
    const AudioTimeStamp*, AudioBufferList* out, const AudioTimeStamp*, void*)
{
    // Tone only for the first 1 s, so we can measure onset latency and decay.
    if (out && out->mNumberBuffers) {
        float* o = (float*)out->mBuffers[0].mData;
        UInt32 ch = out->mBuffers[0].mNumberChannels, n = out->mBuffers[0].mDataByteSize / (ch * 4);
        size_t p = gPlayPos;
        for (UInt32 i = 0; i < n; i++, p++) {
            float v = p < 48000 ? 0.5f * (float)sin(2 * M_PI * kFreq * p / kRate) : 0.f;
            for (UInt32 c = 0; c < ch; c++) o[i * ch + c] = v;
        }
        gPlayPos = p;
    }
    if (in && in->mNumberBuffers) {
        const float* s = (const float*)in->mBuffers[0].mData;
        UInt32 ch = in->mBuffers[0].mNumberChannels, n = in->mBuffers[0].mDataByteSize / (ch * 4);
        size_t p = gRecPos;
        for (UInt32 i = 0; i < n && p < kFrames; i++, p++) gRec[p * 2] = s[i * ch];
        gRecPos = p;
    }
    return noErr;
}

int main()
{
    CFStringRef uid = CFSTR("SlowikMic_UID");
    AudioObjectID dev = 0;
    AudioValueTranslation t = { &uid, sizeof(uid), &dev, sizeof(dev) };
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDeviceForUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 sz = sizeof(t);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, &t) != noErr || !dev) {
        std::printf("FAIL: device SlowikMic_UID not found (driver installed? coreaudiod restarted?)\n");
        return 2;
    }
    AudioDeviceIOProcID id;
    if (AudioDeviceCreateIOProcID(dev, ioProc, nullptr, &id) != noErr || AudioDeviceStart(dev, id) != noErr) {
        std::printf("FAIL: cannot start IO (mic permission? the input side may need microphone access)\n");
        return 3;
    }
    sleep(2);
    AudioDeviceStop(dev, id);
    AudioDeviceDestroyIOProcID(dev, id);

    size_t n = gRecPos;
    // Onset: first frame whose |x| > 0.1. Tail: peak in the last 0.5 s (tone ended at 1 s).
    long onset = -1;
    for (size_t i = 0; i < n; i++) if (fabsf(gRec[i * 2]) > 0.1f) { onset = (long)i; break; }
    double rms = 0; size_t cnt = 0;
    if (onset >= 0) for (size_t i = onset; i < onset + 24000 && i < n; i++, cnt++) rms += gRec[i * 2] * gRec[i * 2];
    rms = cnt ? sqrt(rms / cnt) : 0;
    float tail = 0;
    for (size_t i = n > 24000 ? n - 24000 : 0; i < n; i++) tail = fmaxf(tail, fabsf(gRec[i * 2]));
    std::printf("recorded %zu frames; tone RMS %.3f (expect ~0.354)\n", n, rms);
    if (onset < 0) { std::printf("FAIL: no tone seen on input\n"); return 1; }
    // Playback started at play-frame 0 about when recording started, so onset ~= round trip.
    std::printf("estimated loopback latency: ~%.1f ms (%ld frames)\n", 1000.0 * onset / kRate, onset);
    std::printf("tail peak after tone stopped: %.4f (%s)\n", tail, tail < 0.001f ? "silence OK" : "STALE AUDIO?");
    bool ok = rms > 0.3 && tail < 0.001f;
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
