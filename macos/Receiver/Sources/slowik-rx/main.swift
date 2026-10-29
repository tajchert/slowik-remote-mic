// slowik-rx: headless Slowik receiver (same engine + network as the app). Integration-test harness.
import AVFoundation
import Foundation
import SlowikCore
import SlowikEngine

let usage = """
usage: slowik-rx [--port N] [--wav out.wav] [--play slowik|default] [--seconds N] [--live] [--no-bonjour] [--trim dB] [--bypass]
  Receives Slowik AUDIO on udp/PORT (default 47800), runs jitter buffer + drift compensation in real time,
  prints stats once per second and replies with CONTROL packets.
  --wav       write output: engine output (pre-DSP) when not playing; post-DSP mix when --play is used
  --play      play through the AVAudioEngine DSP chain to the Slowik Mic device or the default output
  --seconds   exit after N seconds (default: run until Ctrl-C)
"""

var port = UInt16(SLOWIK_DEFAULT_PORT)
var wavPath: String?
var play: OutputTarget = .none
var seconds: Double = 0
var live = false
var advertise = true
var trimDb: Float = 12
var bypass = false
var args = CommandLine.arguments.dropFirst().makeIterator()
while let a = args.next() {
    switch a {
    case "--port": port = UInt16(args.next() ?? "") ?? port
    case "--wav": wavPath = args.next()
    case "--play":
        switch args.next() {
        case "slowik": play = .slowikMic
        case "default": play = .defaultOutput
        default: print(usage); exit(2)
        }
    case "--seconds": seconds = Double(args.next() ?? "") ?? 0
    case "--live": live = true
    case "--no-bonjour": advertise = false
    case "--trim": trimDb = Float(args.next() ?? "") ?? trimDb
    case "--bypass": bypass = true
    default: print(usage); exit(a == "-h" || a == "--help" ? 0 : 2)
    }
}

setvbuf(stdout, nil, _IOLBF, 0)
let service = ReceiverService(port: port)
service.setLive(live)
do { try service.start(advertise: advertise) } catch {
    fputs("slowik-rx: \(error.localizedDescription)\n", stderr)
    exit(1)
}
print("slowik-rx listening on udp/\(port) — \(localIPv4Addresses().map { "slowik://\($0):\(port)" }.joined(separator: " "))")

let wav: WavWriter? = try wavPath.map { try WavWriter(path: $0, channels: play == .none ? 1 : 2) }
let wavLock = NSLock()
var clock: ClockRenderer?
var output: AudioOutput?

if play == .none {
    // No device: render from the host clock in 128-frame blocks, like a 128-frame device IO buffer.
    var pending = [Float]()
    pending.reserveCapacity(48000)
    let c = ClockRenderer(engine: service.engine)
    c.start { block in
        guard let w = wav else { return }
        pending.append(contentsOf: block)
        if pending.count >= 24000 {
            wavLock.withLock { pending.withUnsafeBufferPointer { w.write($0) } }
            pending.removeAll(keepingCapacity: true)
        }
    }
    clock = c
} else {
    let o = AudioOutput(engine: service.engine, postTap: wav.map { w in
        { buf in
            // Interleave the (dual-mono) mix for the WAV. Tap thread, not the render thread.
            let n = Int(buf.frameLength), ch = Int(buf.format.channelCount)
            guard let data = buf.floatChannelData else { return }
            var inter = [Float](repeating: 0, count: n * 2)
            for i in 0..<n {
                inter[2 * i] = data[0][i]
                inter[2 * i + 1] = data[min(1, ch - 1)][i]
            }
            wavLock.withLock { inter.withUnsafeBufferPointer { w.write($0) } }
        }
    })
    o.trimDb = trimDb
    o.dspBypass = bypass
    o.onStatusChange = { print("output: \($0.text)") }
    o.start(target: play)
    output = o
}

let t0 = Date()
func printStats() {
    let s = service.stats()
    let t = Date().timeIntervalSince(t0)
    let batt = s.battery >= 0 ? "\(s.battery)%" : "?"
    print(String(format: "t=%3.0fs %@ pkts=%llu lostSeq=%llu foreign=%llu loss=%.1f‰ concealed=%llu late=%llu dup=%llu underruns=%llu dropped=%llu blk=%d resyncs=%llu jit=%.1fms target=%.1fms fill=%.1fms drift=%+.0fppm ratio=%+.0fppm lat=%.0fms level=%.0fdBFS batt=%@ rssi=%d",
                 t, s.linkText, s.packets_received, s.packets_lost_seq, s.packets_foreign, s.loss_permille, s.frames_concealed,
                 s.frames_late, s.frames_duplicate, s.underruns, s.frames_dropped, s.render_block, s.resyncs, s.jitter_ms, s.target_ms, s.fill_ms,
                 s.drift_ppm, s.ratio_ppm, s.latency_ms, dbfs(s.peak), batt, s.rssi)
          + (output.map { " out=\($0.status.text) post=\(Int(dbfs($0.postPeak)))dBFS" } ?? ""))
}

func finish() {
    clock?.stop()
    output?.stop()
    printStats()
    let s = service.stats()
    let played = max(Double(s.frames_played), 1)
    print(String(format: "summary: packets=%llu lost_before_redundancy=%.2f%% lost_after_redundancy=%.3f%% underruns=%llu late=%llu link_lost_events=%llu final_drift=%+.1fppm target=%.1fms",
                 s.packets_received, 100 * Double(s.packets_lost_seq) / max(Double(s.packets_received + s.packets_lost_seq), 1),
                 100 * Double(s.frames_concealed) / played, s.underruns, s.frames_late, s.link_lost_events, s.drift_ppm, s.target_ms))
    wavLock.withLock { wav?.close() }
    if let p = wavPath { print("wrote \(p)") }
    exit(0)
}

let timer = Timer(timeInterval: 1, repeats: true) { _ in
    printStats()
    if seconds > 0 && Date().timeIntervalSince(t0) >= seconds { finish() }
}
RunLoop.main.add(timer, forMode: .common)
signal(SIGINT, SIG_IGN)
let sigint = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
sigint.setEventHandler { finish() }
sigint.resume()
RunLoop.main.run()
