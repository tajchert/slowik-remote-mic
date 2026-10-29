import Foundation
import Darwin
import SlowikEngine

/// Monotonic clock in microseconds (same clock for all arrival timestamps).
@inline(__always) public func monotonicMicros() -> UInt64 {
    clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000
}

/// BSD-socket UDP listener on a dedicated high-priority thread.
/// Pushes AUDIO packets into the engine and sends CONTROL packets (5 Hz) back to the last audio source.
public final class UDPReceiver {
    public let port: UInt16
    private let engine: OpaquePointer
    private var fd: Int32 = -1
    private var thread: Thread?
    private let lock = NSLock()
    private var running = false

    public init(engine: OpaquePointer, port: UInt16 = UInt16(SLOWIK_DEFAULT_PORT)) {
        self.engine = engine
        self.port = port
    }

    deinit { stop() }

    /// Binds 0.0.0.0:port and starts the thread. Throws a descriptive error if the port is taken.
    public func start() throws {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        guard fd >= 0 else { throw posixError("socket") }
        var yes: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, socklen_t(MemoryLayout<Int32>.size))
        var rcvbuf: Int32 = 1 << 20
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, socklen_t(MemoryLayout<Int32>.size))
        var tv = timeval(tv_sec: 0, tv_usec: 20_000)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        addr.sin_addr = in_addr(s_addr: INADDR_ANY)
        let r = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) }
        }
        guard r == 0 else {
            let err = posixError("bind udp/\(port)")
            close(fd)
            fd = -1
            throw err
        }
        lock.withLock { running = true }
        let t = Thread { [unowned self] in self.loop() }
        t.name = "slowik.net"
        t.qualityOfService = .userInteractive
        thread = t
        t.start()
    }

    public func stop() {
        lock.withLock { running = false }
        // The loop wakes up within the 20 ms receive timeout and closes the socket.
        while thread != nil && !(thread!.isFinished) { usleep(5_000) }
        thread = nil
    }

    private var isRunning: Bool { lock.withLock { running } }

    private func loop() {
        var buf = [UInt8](repeating: 0, count: 2048)
        var peer = sockaddr_storage()
        var peerLen: socklen_t = 0
        var lastAudio: UInt64 = 0
        var lastControl: UInt64 = 0
        var ctl = [UInt8](repeating: 0, count: Int(SLOWIK_CONTROL_BYTES))
        while isRunning {
            var from = sockaddr_storage()
            var fromLen = socklen_t(MemoryLayout<sockaddr_storage>.size)
            let n = buf.withUnsafeMutableBytes { p in
                withUnsafeMutablePointer(to: &from) {
                    $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { recvfrom(fd, p.baseAddress, p.count, 0, $0, &fromLen) }
                }
            }
            let now = monotonicMicros()
            if n > 0 {
                let r = slowik_engine_push_packet(engine, buf, n, now)
                if r == 0 || r == Int32(SLOWIK_PUSH_QUEUE_FULL) {
                    peer = from
                    peerLen = fromLen
                    lastAudio = now
                }
            }
            // CONTROL at 5 Hz to the most recent audio source (kept for 10 s after the last packet).
            if peerLen > 0 && now &- lastControl >= 200_000 && now &- lastAudio < 10_000_000 {
                lastControl = now
                let len = slowik_engine_build_control(engine, &ctl, ctl.count)
                _ = withUnsafePointer(to: &peer) {
                    $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { sendto(fd, ctl, len, 0, $0, peerLen) }
                }
            }
        }
        close(fd)
        fd = -1
    }

    private func posixError(_ what: String) -> NSError {
        NSError(domain: NSPOSIXErrorDomain, code: Int(errno),
                userInfo: [NSLocalizedDescriptionKey: "\(what): \(String(cString: strerror(errno)))"])
    }
}

/// Bonjour advertisement `_slowik._udp.` named "Slowik on <host>". Needs a running main run loop.
public final class BonjourAdvertiser: NSObject, NetServiceDelegate {
    private var service: NetService?
    public private(set) var published = false
    public private(set) var lastError: String?

    public static var defaultName: String {
        let host = Host.current().localizedName ?? ProcessInfo.processInfo.hostName
        return "Slowik on \(host)"
    }

    public func start(port: UInt16, name: String = BonjourAdvertiser.defaultName) {
        let s = NetService(domain: "local.", type: "_slowik._udp.", name: name, port: Int32(port))
        s.delegate = self
        s.publish()
        service = s
    }

    public func stop() {
        service?.stop()
        service = nil
        published = false
    }

    public func netServiceDidPublish(_ sender: NetService) { published = true }
    public func netService(_ sender: NetService, didNotPublish errorDict: [String: NSNumber]) {
        lastError = "\(errorDict)"
    }
}

/// Non-loopback IPv4 addresses of this Mac (for the "slowik://ip:port" text/QR).
public func localIPv4Addresses() -> [String] {
    var result: [String] = []
    var ifaddr: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&ifaddr) == 0, let first = ifaddr else { return [] }
    defer { freeifaddrs(ifaddr) }
    for p in sequence(first: first, next: { $0.pointee.ifa_next }) {
        let ifa = p.pointee
        guard let sa = ifa.ifa_addr, sa.pointee.sa_family == UInt8(AF_INET) else { continue }
        let flags = Int32(ifa.ifa_flags)
        guard flags & IFF_UP != 0, flags & IFF_LOOPBACK == 0 else { continue }
        var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
        if getnameinfo(sa, socklen_t(sa.pointee.sa_len), &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0 {
            let ip = String(cString: host)
            if !ip.hasPrefix("169.254.") { result.append(ip) }
        }
    }
    return result
}
