import Foundation

/// Minimal mono/stereo 16-bit PCM WAV writer (not real-time safe; use off the audio thread).
public final class WavWriter {
    private let handle: FileHandle
    private let channels: Int
    private let rate: Int
    private var dataBytes = 0

    public init(path: String, sampleRate: Int = 48000, channels: Int = 1) throws {
        FileManager.default.createFile(atPath: path, contents: nil)
        guard let h = FileHandle(forWritingAtPath: path) else {
            throw NSError(domain: "WavWriter", code: 1, userInfo: [NSLocalizedDescriptionKey: "cannot open \(path)"])
        }
        handle = h
        self.channels = channels
        rate = sampleRate
        handle.write(header(dataBytes: 0))
    }

    /// Interleaved float samples in [-1, 1].
    public func write(_ samples: UnsafeBufferPointer<Float>) {
        var out = Data(count: samples.count * 2)
        out.withUnsafeMutableBytes { raw in
            let p = raw.bindMemory(to: Int16.self)
            for i in 0..<samples.count {
                let c = max(-1, min(1, samples[i]))
                p[i] = Int16(c * 32767).littleEndian
            }
        }
        handle.write(out)
        dataBytes += out.count
    }

    public func close() {
        handle.seek(toFileOffset: 0)
        handle.write(header(dataBytes: dataBytes))
        handle.closeFile()
    }

    private func header(dataBytes: Int) -> Data {
        var d = Data()
        func u32(_ v: Int) { var x = UInt32(v).littleEndian; d.append(Data(bytes: &x, count: 4)) }
        func u16(_ v: Int) { var x = UInt16(v).littleEndian; d.append(Data(bytes: &x, count: 2)) }
        d.append("RIFF".data(using: .ascii)!)
        u32(36 + dataBytes)
        d.append("WAVEfmt ".data(using: .ascii)!)
        u32(16); u16(1); u16(channels); u32(rate); u32(rate * channels * 2); u16(channels * 2); u16(16)
        d.append("data".data(using: .ascii)!)
        u32(dataBytes)
        return d
    }
}
