import SwiftUI
import SlowikCore
import SlowikEngine

@main
struct SlowikReceiverApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        MenuBarExtra {
            MenuView().environmentObject(model)
        } label: {
            Image(nsImage: model.menuIcon)
        }
        .menuBarExtraStyle(.window)
    }
}

struct MenuView: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            header
            liveControls
            meters
            Divider()
            statsGrid
            Divider()
            outputSection
            Divider()
            connectSection
            HStack {
                Spacer()
                Button("Quit Slowik") { NSApp.terminate(nil) }.keyboardShortcut("q")
            }
        }
        .padding(14)
        .frame(width: 340)
    }

    private var statusColor: Color {
        switch m.linkState {
        case Int32(SLOWIK_LINK_CONNECTED): return .green
        case Int32(SLOWIK_LINK_LOST): return .red
        default: return .secondary
        }
    }

    private var header: some View {
        HStack(alignment: .firstTextBaseline) {
            Circle().fill(statusColor).frame(width: 10, height: 10)
            Text(m.statusText).font(.headline)
            Spacer()
            if m.stats.link_state != Int32(SLOWIK_LINK_WAITING) {
                Label(m.stats.battery >= 0 ? "\(m.stats.battery)%" : "?",
                      systemImage: m.stats.flags & 2 != 0 ? "battery.100.bolt" : "battery.75")
                    .font(.caption)
                Label(m.stats.rssi != 0 ? "\(m.stats.rssi) dBm" : "?", systemImage: "wifi").font(.caption)
            }
        }
    }

    private var liveControls: some View {
        HStack(spacing: 10) {
            Button { m.live.toggle() } label: {
                Text(m.live ? "LIVE" : "Go LIVE")
                    .font(.title2.bold())
                    .frame(maxWidth: .infinity, minHeight: 44)
                    .foregroundStyle(m.live ? .white : .primary)
                    .background(RoundedRectangle(cornerRadius: 8).fill(m.live ? Color.red : Color.secondary.opacity(0.15)))
            }
            .buttonStyle(.plain)
            Button { m.muted.toggle() } label: {
                Label(m.muted ? "MUTED" : "Mute", systemImage: m.muted ? "mic.slash.fill" : "mic.fill")
                    .font(.headline)
                    .frame(width: 110, height: 44)
                    .foregroundStyle(m.muted ? .white : .primary)
                    .background(RoundedRectangle(cornerRadius: 8).fill(m.muted ? Color.orange : Color.secondary.opacity(0.15)))
            }
            .buttonStyle(.plain)
        }
    }

    private var meters: some View {
        VStack(alignment: .leading, spacing: 6) {
            MeterRow(label: "In", peak: m.stats.peak)
            MeterRow(label: "Out", peak: m.postPeak)
            HStack {
                Text("Trim").font(.caption).frame(width: 34, alignment: .leading)
                Slider(value: $m.trimDb, in: -24...24, step: 1)
                Text(String(format: "%+.0f dB", m.trimDb)).font(.caption.monospacedDigit()).frame(width: 46)
            }
        }
    }

    private var statsGrid: some View {
        let s = m.stats
        let lossPct = s.loss_permille / 10
        return Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 4) {
            GridRow {
                stat("Loss", String(format: "%.1f %%", lossPct), warn: lossPct > 1)
                stat("Latency", s.latency_ms > 0 ? String(format: "%.0f ms", s.latency_ms) : "–", warn: s.latency_ms > 50)
            }
            GridRow {
                stat("Buffer", String(format: "%.0f / %.0f ms", s.fill_ms, s.target_ms), warn: false)
                stat("Jitter", String(format: "%.1f ms", s.jitter_ms), warn: s.jitter_ms > 20)
            }
            GridRow {
                stat("Drift", String(format: "%+.0f ppm", s.drift_ppm), warn: abs(s.drift_ppm) > 180)
                stat("Dropouts", "\(s.frames_concealed + s.underruns)", warn: false)
            }
        }
    }

    private func stat(_ k: String, _ v: String, warn: Bool) -> some View {
        HStack(spacing: 4) {
            Text(k).font(.caption).foregroundStyle(.secondary)
            Text(v).font(.caption.monospacedDigit()).foregroundStyle(warn ? .orange : .primary)
        }
    }

    private var outputSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Picker("Output", selection: $m.outputTarget) {
                ForEach(OutputTarget.allCases) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.menu)
            HStack {
                Image(systemName: m.outputStatus == .driverMissing ? "exclamationmark.triangle.fill" : "speaker.wave.2")
                    .foregroundStyle(m.outputStatus == .driverMissing ? .orange : .secondary)
                Text(m.outputStatus.text).font(.caption)
            }
            if m.outputStatus == .driverMissing {
                Button("Monitor on default output instead") { m.outputTarget = .defaultOutput }.font(.caption)
            }
            Toggle("Bypass voice processing (DSP)", isOn: $m.dspBypass).font(.caption)
        }
    }

    private var connectSection: some View {
        HStack(alignment: .top, spacing: 12) {
            if let first = m.urls.first, let qr = m.qrImage(for: first) {
                Image(nsImage: qr).interpolation(.none).resizable().frame(width: 96, height: 96)
            }
            VStack(alignment: .leading, spacing: 4) {
                Text("Connect the phone to").font(.caption).foregroundStyle(.secondary)
                if m.urls.isEmpty { Text("No network").font(.caption) }
                ForEach(m.urls, id: \.self) { Text($0).font(.caption.monospaced()).textSelection(.enabled) }
                Text("or pick “\(BonjourAdvertiser.defaultName)” in the app").font(.caption2).foregroundStyle(.secondary)
            }
        }
    }
}

struct MeterRow: View {
    let label: String
    let peak: Float

    var body: some View {
        let db = max(-60, dbfs(peak))
        HStack {
            Text(label).font(.caption).frame(width: 34, alignment: .leading)
            GeometryReader { g in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3).fill(Color.secondary.opacity(0.2))
                    RoundedRectangle(cornerRadius: 3)
                        .fill(db > -3 ? Color.red : (db > -12 ? Color.yellow : Color.green))
                        .frame(width: g.size.width * CGFloat((db + 60) / 60))
                }
            }
            .frame(height: 8)
            Text(peak > 1e-6 ? String(format: "%.0f", db) : "–").font(.caption.monospacedDigit()).frame(width: 46)
        }
    }
}
