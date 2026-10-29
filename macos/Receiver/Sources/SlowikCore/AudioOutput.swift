import AVFoundation
import AudioToolbox
import CoreAudio
import Foundation
import SlowikEngine

public enum OutputTarget: String, CaseIterable, Identifiable {
    case slowikMic = "Slowik Mic"
    case defaultOutput = "Default output (monitor)"
    case none = "Off"
    public var id: String { rawValue }
}

public enum OutputStatus: Equatable {
    case stopped
    case running(device: String)
    case driverMissing          // target is Slowik Mic but the driver is not installed
    case failed(String)

    public var text: String {
        switch self {
        case .stopped: return "Output off"
        case .running(let d): return "Playing to \(d)"
        case .driverMissing: return "Slowik Mic driver not installed"
        case .failed(let e): return "Output error: \(e)"
        }
    }
}

/// AVAudioEngine output: engine (source node) -> trim -> high-pass -> dynamics -> peak limiter -> -1 dB -> device.
/// Plays dual-mono into a 2-channel device. Main-thread API.
public final class AudioOutput {
    public private(set) var status: OutputStatus = .stopped
    public private(set) var target: OutputTarget = .none
    public var onStatusChange: ((OutputStatus) -> Void)?

    /// Input trim in dB (default +12, range -24...+24).
    public var trimDb: Float = 12 { didSet { trim.globalGain = trimDb } }
    /// Bypasses high-pass, dynamics and limiter (trim stays).
    public var dspBypass = false { didSet { applyBypass() } }

    private let slowik: OpaquePointer
    private var engine = AVAudioEngine()
    private let format = AVAudioFormat(standardFormatWithSampleRate: Double(SLOWIK_SAMPLE_RATE), channels: 2)!
    private let trim = AVAudioUnitEQ(numberOfBands: 0)
    private let highPass = AVAudioUnitEQ(numberOfBands: 2)
    private let dynamics = AVAudioUnitEffect(audioComponentDescription: AudioOutput.appleEffect(kAudioUnitSubType_DynamicsProcessor))
    private let limiter = AVAudioUnitEffect(audioComponentDescription: AudioOutput.appleEffect(kAudioUnitSubType_PeakLimiter))
    private var source: AVAudioSourceNode!
    private var deviceToken: AnyObject?
    private var configObserver: NSObjectProtocol?
    private var attachedDevice: AudioDeviceID?
    private let meterLock = NSLock()
    private var postPeakValue: Float = 0
    private var tapHandler: ((AVAudioPCMBuffer) -> Void)?

    /// Optional post-DSP sink (e.g. WAV writer in the CLI). Called on the tap thread, not the render thread.
    public init(engine slowik: OpaquePointer, postTap: ((AVAudioPCMBuffer) -> Void)? = nil) {
        self.slowik = slowik
        self.tapHandler = postTap
        trim.globalGain = trimDb
        for (i, band) in highPass.bands.enumerated() {
            // Two cascaded 12 dB/oct high-pass sections = 24 dB/oct at ~90 Hz.
            band.filterType = .highPass
            band.frequency = 90
            band.bandwidth = 0.5
            band.bypass = false
            _ = i
        }
        configureDynamics()
        deviceToken = AudioDevices.observeChanges { [weak self] in self?.devicesChanged() }
    }

    deinit { stop() }

    public var postPeak: Float { meterLock.withLock { postPeakValue } }

    private static func appleEffect(_ sub: OSType) -> AudioComponentDescription {
        AudioComponentDescription(componentType: kAudioUnitType_Effect, componentSubType: sub,
                                  componentManufacturer: kAudioUnitManufacturer_Apple, componentFlags: 0, componentFlagsMask: 0)
    }

    private func configureDynamics() {
        // Compressor-ish starting point (plan table, stage 4 + 6): gentle 2:1 expander below -50 dBFS,
        // compression from -24 dBFS with fast attack / medium release, +4 dB make-up.
        let d = dynamics.audioUnit
        AudioUnitSetParameter(d, kDynamicsProcessorParam_Threshold, kAudioUnitScope_Global, 0, -24, 0)
        AudioUnitSetParameter(d, kDynamicsProcessorParam_HeadRoom, kAudioUnitScope_Global, 0, 8, 0)
        AudioUnitSetParameter(d, kDynamicsProcessorParam_ExpansionRatio, kAudioUnitScope_Global, 0, 2, 0)
        AudioUnitSetParameter(d, kDynamicsProcessorParam_ExpansionThreshold, kAudioUnitScope_Global, 0, -50, 0)
        AudioUnitSetParameter(d, kDynamicsProcessorParam_AttackTime, kAudioUnitScope_Global, 0, 0.007, 0)
        AudioUnitSetParameter(d, kDynamicsProcessorParam_ReleaseTime, kAudioUnitScope_Global, 0, 0.15, 0)
        AudioUnitSetParameter(d, kDynamicsProcessorParam_OverallGain, kAudioUnitScope_Global, 0, 4, 0)
        let l = limiter.audioUnit
        AudioUnitSetParameter(l, kLimiterParam_AttackTime, kAudioUnitScope_Global, 0, 0.002, 0)
        AudioUnitSetParameter(l, kLimiterParam_DecayTime, kAudioUnitScope_Global, 0, 0.05, 0)
        AudioUnitSetParameter(l, kLimiterParam_PreGain, kAudioUnitScope_Global, 0, 0, 0)
    }

    private func applyBypass() {
        highPass.bypass = dspBypass
        dynamics.bypass = dspBypass
        limiter.bypass = dspBypass
    }

    /// Starts (or restarts) playback to `target`. Returns the resulting status.
    @discardableResult
    public func start(target: OutputTarget) -> OutputStatus {
        self.target = target
        teardown()
        guard target != .none else { return setStatus(.stopped) }
        let dev: AudioDeviceID?
        switch target {
        case .slowikMic: dev = AudioDevices.device(forUID: slowikMicUID)
        case .defaultOutput: dev = AudioDevices.defaultOutput()
        case .none: dev = nil
        }
        guard let device = dev else {
            return setStatus(target == .slowikMic ? .driverMissing : .failed("no output device"))
        }
        engine = AVAudioEngine()
        // The render block is the real-time path: one C call + a memcpy for the second channel.
        let slowik = self.slowik
        source = AVAudioSourceNode(format: format) { _, _, frameCount, abl -> OSStatus in
            let list = UnsafeMutableAudioBufferListPointer(abl)
            guard let ch0 = list[0].mData?.assumingMemoryBound(to: Float.self) else { return noErr }
            slowik_engine_render(slowik, ch0, Int32(frameCount))
            for i in 1..<list.count {
                if let ch = list[i].mData { memcpy(ch, ch0, Int(frameCount) * MemoryLayout<Float>.size) }
            }
            return noErr
        }
        guard let outUnit = engine.outputNode.audioUnit else { return setStatus(.failed("no output unit")) }
        var devID = device
        let st = AudioUnitSetProperty(outUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                      &devID, UInt32(MemoryLayout<AudioDeviceID>.size))
        guard st == noErr else { return setStatus(.failed("set device (\(st))")) }
        AudioDevices.setBufferFrameSize(device, 128)

        for node in [source!, trim, highPass, dynamics, limiter] as [AVAudioNode] { engine.attach(node) }
        engine.connect(source, to: trim, format: format)
        engine.connect(trim, to: highPass, format: format)
        engine.connect(highPass, to: dynamics, format: format)
        engine.connect(dynamics, to: limiter, format: format)
        engine.connect(limiter, to: engine.mainMixerNode, format: format)
        let hwFormat = engine.outputNode.outputFormat(forBus: 0)
        engine.connect(engine.mainMixerNode, to: engine.outputNode, format: hwFormat)
        engine.mainMixerNode.outputVolume = 0.891  // limiter ceiling 0 dBFS -> -1 dBFS
        applyBypass()
        engine.mainMixerNode.installTap(onBus: 0, bufferSize: 1024, format: nil) { [weak self] buf, _ in
            guard let self else { return }
            var peak: Float = 0
            if let ch = buf.floatChannelData?[0] {
                for i in 0..<Int(buf.frameLength) { peak = max(peak, abs(ch[i])) }
            }
            self.meterLock.withLock { self.postPeakValue = peak }
            self.tapHandler?(buf)
        }
        configObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.start(target: self.target)  // device format changed / coreaudiod restarted: re-attach
        }
        engine.prepare()
        do {
            try engine.start()
        } catch {
            teardown()
            return setStatus(.failed(error.localizedDescription))
        }
        attachedDevice = device
        slowik_engine_set_output_latency_ms(slowik, AudioDevices.outputLatencyMs(device))
        return setStatus(.running(device: AudioDevices.name(device)))
    }

    public func stop() {
        target = .none
        teardown()
        _ = setStatus(.stopped)
    }

    private func teardown() {
        if let o = configObserver { NotificationCenter.default.removeObserver(o) }
        configObserver = nil
        if engine.isRunning || attachedDevice != nil {
            engine.mainMixerNode.removeTap(onBus: 0)
            engine.stop()
        }
        for node in [source, trim, highPass, dynamics, limiter] as [AVAudioNode?] {
            if let n = node, n.engine != nil { engine.detach(n) }
        }
        attachedDevice = nil
    }

    private func devicesChanged() {
        switch target {
        case .none: return
        case .slowikMic:
            let dev = AudioDevices.device(forUID: slowikMicUID)
            if dev != attachedDevice || !engine.isRunning { start(target: .slowikMic) }
        case .defaultOutput:
            if AudioDevices.defaultOutput() != attachedDevice || !engine.isRunning { start(target: .defaultOutput) }
        }
    }

    @discardableResult
    private func setStatus(_ s: OutputStatus) -> OutputStatus {
        status = s
        onStatusChange?(s)
        return s
    }
}
