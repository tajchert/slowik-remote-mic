import Foundation
import Darwin
import SlowikEngine

/// Owns the engine and the network side (UDP + CONTROL + Bonjour). Audio output is attached separately.
public final class ReceiverService {
    public let engine: OpaquePointer
    public let port: UInt16
    public let udp: UDPReceiver
    public let bonjour = BonjourAdvertiser()

    public init(port: UInt16 = UInt16(SLOWIK_DEFAULT_PORT)) {
        engine = slowik_engine_create(nil)!
        self.port = port
        udp = UDPReceiver(engine: engine, port: port)
    }

    deinit {
        udp.stop()
        bonjour.stop()
        slowik_engine_destroy(engine)
    }

    public func start(advertise: Bool = true) throws {
        try udp.start()
        if advertise { bonjour.start(port: port) }
    }

    public func stats() -> SlowikStats {
        var s = SlowikStats()
        slowik_engine_get_stats(engine, &s)
        return s
    }

    public func setLive(_ on: Bool) { slowik_engine_set_live(engine, on ? 1 : 0) }
    public func setMute(_ on: Bool) { slowik_engine_set_mute(engine, on ? 1 : 0) }
}

/// Drives `slowik_engine_render` from the host clock (no audio device) for the headless CLI.
/// Calls `sink` with each rendered block on its own thread.
public final class ClockRenderer {
    private let engine: OpaquePointer
    private let block: Int
    private var thread: Thread?
    private let lock = NSLock()
    private var running = false

    public init(engine: OpaquePointer, block: Int = 128) {
        self.engine = engine
        self.block = block
    }

    public func start(sink: @escaping (UnsafeBufferPointer<Float>) -> Void) {
        lock.withLock { running = true }
        let t = Thread { [unowned self] in
            var info = mach_timebase_info_data_t()
            mach_timebase_info(&info)
            let periodNs = Double(self.block) * 1e9 / Double(SLOWIK_SAMPLE_RATE)
            let start = mach_absolute_time()
            var buf = [Float](repeating: 0, count: self.block)
            var k: UInt64 = 0
            while self.lock.withLock({ self.running }) {
                k += 1
                let deadlineNs = Double(k) * periodNs
                let ticks = UInt64(deadlineNs * Double(info.denom) / Double(info.numer))
                mach_wait_until(start + ticks)
                buf.withUnsafeMutableBufferPointer { p in
                    slowik_engine_render(self.engine, p.baseAddress!, Int32(self.block))
                    sink(UnsafeBufferPointer(p))
                }
            }
        }
        t.name = "slowik.clock"
        t.qualityOfService = .userInteractive
        thread = t
        t.start()
    }

    public func stop() {
        lock.withLock { running = false }
        while let t = thread, !t.isFinished { usleep(2_000) }
        thread = nil
    }
}

public extension SlowikStats {
    var linkText: String {
        switch link_state {
        case Int32(SLOWIK_LINK_CONNECTED): return "connected"
        case Int32(SLOWIK_LINK_LOST): return "link lost"
        default: return "waiting"
        }
    }
}

public func dbfs(_ x: Float) -> Float { x > 1e-6 ? 20 * log10(x) : -120 }
