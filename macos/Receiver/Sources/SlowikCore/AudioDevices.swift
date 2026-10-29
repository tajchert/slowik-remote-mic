import CoreAudio
import Foundation

/// UID of the "Slowik Mic" loopback device (docs/DECISIONS.md item 5).
public let slowikMicUID = "SlowikMic_UID"

public enum AudioDevices {
    private static func address(_ sel: AudioObjectPropertySelector,
                                _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    }

    /// kAudioHardwarePropertyTranslateUIDToDevice; nil if no such device is present.
    public static func device(forUID uid: String) -> AudioDeviceID? {
        var addr = address(kAudioHardwarePropertyTranslateUIDToDevice)
        var cfUID: CFString = uid as CFString
        var dev = AudioDeviceID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        let st = withUnsafePointer(to: &cfUID) {
            AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr,
                                       UInt32(MemoryLayout<CFString>.size), $0, &size, &dev)
        }
        return st == noErr && dev != kAudioObjectUnknown ? dev : nil
    }

    public static func defaultOutput() -> AudioDeviceID? {
        var addr = address(kAudioHardwarePropertyDefaultOutputDevice)
        var dev = AudioDeviceID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        let st = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &dev)
        return st == noErr && dev != kAudioObjectUnknown ? dev : nil
    }

    public static func name(_ dev: AudioDeviceID) -> String {
        var addr = address(kAudioObjectPropertyName)
        var name: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        guard AudioObjectGetPropertyData(dev, &addr, 0, nil, &size, &name) == noErr, let n = name else { return "?" }
        return n.takeRetainedValue() as String
    }

    private static func u32(_ dev: AudioDeviceID, _ sel: AudioObjectPropertySelector,
                            _ scope: AudioObjectPropertyScope) -> UInt32 {
        var addr = address(sel, scope)
        var v: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        return AudioObjectGetPropertyData(dev, &addr, 0, nil, &size, &v) == noErr ? v : 0
    }

    public static func nominalSampleRate(_ dev: AudioDeviceID) -> Double {
        var addr = address(kAudioDevicePropertyNominalSampleRate)
        var v: Float64 = 0
        var size = UInt32(MemoryLayout<Float64>.size)
        return AudioObjectGetPropertyData(dev, &addr, 0, nil, &size, &v) == noErr ? v : 0
    }

    /// Requests a small IO buffer (frames). Returns the buffer size actually in effect.
    @discardableResult
    public static func setBufferFrameSize(_ dev: AudioDeviceID, _ frames: UInt32) -> UInt32 {
        var addr = address(kAudioDevicePropertyBufferFrameSize)
        var v = frames
        AudioObjectSetPropertyData(dev, &addr, 0, nil, UInt32(MemoryLayout<UInt32>.size), &v)
        return u32(dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal)
    }

    /// Output-side latency in ms: device latency + safety offset + IO buffer.
    public static func outputLatencyMs(_ dev: AudioDeviceID) -> Double {
        let scope = kAudioDevicePropertyScopeOutput
        let rate = max(nominalSampleRate(dev), 1)
        let frames = u32(dev, kAudioDevicePropertyLatency, scope) + u32(dev, kAudioDevicePropertySafetyOffset, scope)
            + u32(dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal)
        return Double(frames) * 1000.0 / rate
    }

    /// Calls `handler` on the main queue whenever the device list or the default output changes
    /// (driver installed/removed, coreaudiod restart). Keep the returned token alive.
    public static func observeChanges(_ handler: @escaping () -> Void) -> AnyObject {
        let token = ListenerToken(handler)
        for sel in [kAudioHardwarePropertyDevices, kAudioHardwarePropertyDefaultOutputDevice] {
            var addr = address(sel)
            AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &addr, .main, token.block)
        }
        return token
    }

    private final class ListenerToken {
        let block: AudioObjectPropertyListenerBlock
        init(_ h: @escaping () -> Void) { block = { _, _ in h() } }
        deinit {
            for sel in [kAudioHardwarePropertyDevices, kAudioHardwarePropertyDefaultOutputDevice] {
                var addr = AudioDevices.address(sel)
                AudioObjectRemovePropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &addr, .main, block)
            }
        }
    }
}
