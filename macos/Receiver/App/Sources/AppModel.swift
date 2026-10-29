import AppKit
import CoreImage.CIFilterBuiltins
import Foundation
import SlowikCore
import SlowikEngine

/// UI state. Polls the engine stats at 10 Hz; all mutations happen on the main thread.
@MainActor
final class AppModel: ObservableObject {
    @Published var stats = SlowikStats()
    @Published var live = false { didSet { service.setLive(live) } }
    @Published var muted = false { didSet { service.setMute(muted) } }
    @Published var trimDb: Double = 12 { didSet { output.trimDb = Float(trimDb) } }
    @Published var dspBypass = false { didSet { output.dspBypass = dspBypass } }
    @Published var outputTarget: OutputTarget = .slowikMic { didSet { if oldValue != outputTarget { output.start(target: outputTarget) } } }
    @Published private(set) var outputStatus: OutputStatus = .stopped
    @Published private(set) var postPeak: Float = 0
    @Published private(set) var addresses: [String] = []
    @Published private(set) var networkError: String?

    let service = ReceiverService()
    let output: AudioOutput
    private var timer: Timer?

    init() {
        output = AudioOutput(engine: service.engine, idleRender: true)
        output.trimDb = Float(trimDb)
        output.onStatusChange = { [weak self] s in
            Task { @MainActor in self?.outputStatus = s }
        }
        do { try service.start() } catch { networkError = error.localizedDescription }
        outputStatus = output.start(target: outputTarget)
        refreshAddresses()
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
    }

    private var ticks = 0
    private func tick() {
        stats = service.stats()
        postPeak = output.postPeak
        ticks += 1
        if ticks % 50 == 0 { refreshAddresses() }  // every 5 s (Wi-Fi changes)
    }

    func refreshAddresses() {
        let a = localIPv4Addresses()
        if a != addresses { addresses = a }
    }

    var urls: [String] { addresses.map { "slowik://\($0):\(service.port)" } }

    var linkState: Int32 { stats.link_state }

    var statusText: String {
        if let e = networkError { return "Network error: \(e)" }
        switch stats.link_state {
        case Int32(SLOWIK_LINK_CONNECTED): return stats.flags & 1 != 0 ? "Connected (muted on phone)" : "Connected"
        case Int32(SLOWIK_LINK_LOST): return "Link lost"
        default: return "Waiting for phone…"
        }
    }

    var menuIcon: NSImage {
        let lost = stats.link_state == Int32(SLOWIK_LINK_LOST)
        let connected = stats.link_state == Int32(SLOWIK_LINK_CONNECTED)
        let name: String
        let color: NSColor?
        if live && !muted { name = "mic.circle.fill"; color = .systemRed }
        else if muted { name = "mic.slash.fill"; color = live ? .systemRed : nil }
        else if lost { name = "mic.badge.xmark"; color = .systemOrange }
        else if connected { name = "mic.fill"; color = nil }
        else { name = "mic"; color = nil }
        let base = NSImage(systemSymbolName: name, accessibilityDescription: "Slowik") ?? NSImage()
        guard let c = color else {
            base.isTemplate = true
            return base
        }
        let cfg = NSImage.SymbolConfiguration(paletteColors: [c])
        let img = base.withSymbolConfiguration(cfg) ?? base
        img.isTemplate = false
        return img
    }

    func qrImage(for text: String) -> NSImage? {
        let f = CIFilter.qrCodeGenerator()
        f.message = Data(text.utf8)
        f.correctionLevel = "M"
        guard let ci = f.outputImage?.transformed(by: CGAffineTransform(scaleX: 6, y: 6)) else { return nil }
        let rep = NSCIImageRep(ciImage: ci)
        let img = NSImage(size: rep.size)
        img.addRepresentation(rep)
        return img
    }
}
