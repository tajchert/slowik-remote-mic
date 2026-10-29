// Slowik Mic - HAL AudioServerPlugIn built on libASPL.
// Output stream (written by the Slowik receiver app) is looped back to the
// input stream (read by Meet/Zoom/...). Only copies samples.
#include <aspl/Driver.hpp>

#include <CoreAudio/AudioServerPlugIn.h>

#include "LoopbackRing.hpp"

namespace {

constexpr UInt32 kSampleRate = 48000;
constexpr UInt32 kChannels = 2;
// 16384 frames = ~341 ms at 48 kHz; power of two, comfortably above any IO buffer.
constexpr UInt32 kRingFrames = 16384;
// Zero-timestamp period: how often HAL re-syncs the clock. 512 frames (~10.7 ms)
// keeps timestamp jitter low without busy updates; it is independent of the
// IO buffer size clients choose. (libASPL default is 1 s, which makes the DAL
// clock estimate coarse.)
constexpr UInt32 kZeroTsPeriod = 512;

AudioStreamBasicDescription MakeFormat()
{
    AudioStreamBasicDescription f = {};
    f.mSampleRate = kSampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    f.mBitsPerChannel = 32;
    f.mChannelsPerFrame = kChannels;
    f.mBytesPerFrame = kChannels * sizeof(float);
    f.mFramesPerPacket = 1;
    f.mBytesPerPacket = f.mBytesPerFrame;
    return f;
}

class Handler : public aspl::ControlRequestHandler, public aspl::IORequestHandler
{
public:
    OSStatus OnStartIO() override
    {
        ring_.Clear();
        return kAudioHardwareNoError;
    }
    void OnStopIO() override { ring_.Clear(); }

    void OnReadClientInput(const std::shared_ptr<aspl::Client>&,
        const std::shared_ptr<aspl::Stream>&,
        Float64,
        Float64 timestamp,
        void* bytes,
        UInt32 bytesCount) override
    {
        ring_.Read((int64_t)timestamp, (float*)bytes, bytesCount / (kChannels * sizeof(float)));
    }

    void OnWriteMixedOutput(const std::shared_ptr<aspl::Stream>&,
        Float64,
        Float64 timestamp,
        const void* bytes,
        UInt32 bytesCount) override
    {
        ring_.Write((int64_t)timestamp, (const float*)bytes, bytesCount / (kChannels * sizeof(float)));
    }

private:
    slowik::LoopbackRing ring_ { kRingFrames, kChannels };
};

std::shared_ptr<aspl::Driver> CreateDriver()
{
    auto context = std::make_shared<aspl::Context>();

    aspl::DeviceParameters dp;
    dp.Name = "Slowik Mic";
    dp.Manufacturer = "Slowik";
    dp.DeviceUID = "SlowikMic_UID";
    dp.ModelUID = "SlowikMic_ModelUID";
    dp.SampleRate = kSampleRate;
    dp.ChannelCount = kChannels;
    dp.Latency = 0;
    dp.SafetyOffset = 0;
    dp.ZeroTimeStampPeriod = kZeroTsPeriod;
    dp.EnableMixing = true;

    auto device = std::make_shared<aspl::Device>(context, dp);

    aspl::StreamParameters sp;
    sp.Format = MakeFormat();
    sp.Latency = 0;
    sp.Direction = aspl::Direction::Output;
    device->AddStreamAsync(sp);
    sp.Direction = aspl::Direction::Input;
    device->AddStreamAsync(sp);

    auto handler = std::make_shared<Handler>();
    device->SetControlHandler(handler);
    device->SetIOHandler(handler);

    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);
    return std::make_shared<aspl::Driver>(context, plugin);
}

} // namespace

extern "C" void* SlowikMicEntryPoint(CFAllocatorRef, CFUUIDRef typeUUID)
{
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        return nullptr;
    }
    static std::shared_ptr<aspl::Driver> driver = CreateDriver();
    return driver->GetReference();
}
