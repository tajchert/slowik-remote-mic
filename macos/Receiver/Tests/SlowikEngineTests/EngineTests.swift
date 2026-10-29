import Testing
import Foundation
import SlowikEngine
import SlowikSim

private func sim(_ tweak: (inout SlowikSimConfig) -> Void) -> SlowikSimResult {
    var c = SlowikSimConfig()
    slowik_sim_default(&c)
    tweak(&c)
    var r = SlowikSimResult()
    slowik_sim_run(&c, nil, &r)
    return r
}

private func describe(_ r: SlowikSimResult) -> String {
    let s = r.stats
    return "played=\(s.frames_played) concealed=\(s.frames_concealed) late=\(s.frames_late) dup=\(s.frames_duplicate) " +
        "under=\(s.underruns) (afterWarm=\(r.underruns_after_warmup)) resyncs=\(s.resyncs) loss‰=\(s.loss_permille) " +
        "jit=\(s.jitter_ms) target=\(s.target_ms) fill=\(s.fill_ms) fillErrMax=\(r.fill_err_max_ms) " +
        "drift=\(s.drift_ppm) dropped=\(s.frames_dropped) latMax=\(r.latency_max_ms) disc=\(r.discontinuities) maxStep=\(r.max_step) lat=\(s.latency_ms)"
}

// MARK: - Protocol

@Test func parsesPcm16AndPcm24() {
    var frames = [Float](repeating: 0, count: 480)
    for i in 0..<480 { frames[i] = Float(i % 100) / 200 - 0.25 }
    for codec in [UInt8(1), UInt8(2)] {
        var buf = [UInt8](repeating: 0, count: 2048)
        let len = slowik_build_audio(&buf, buf.count, 3, 0xFFFF_FFFE, 4800, 123_456, 77, -42, 2, codec, frames, 2)
        #expect(len == (codec == 1 ? 992 : 32 + 1440))
        var h = SlowikAudioHeader()
        #expect(slowik_parse_audio(buf, len, &h) == SLOWIK_PARSE_OK)
        #expect(h.stream_id == 3 && h.seq == 0xFFFF_FFFE && h.sample_index == 4800 && h.capture_time_us == 123_456)
        #expect(h.battery == 77 && h.rssi == -42 && h.flags == 2 && h.codec == codec && h.frame_count == 2)
    }
}

@Test func rejectsInvalidPackets() {
    var h = SlowikAudioHeader()
    let short = [UInt8](repeating: 0, count: 20)
    #expect(slowik_parse_audio(short, short.count, &h) == SLOWIK_PARSE_TOO_SHORT)
    var buf = [UInt8](repeating: 0, count: 992)
    let frames = [Float](repeating: 0, count: 480)
    let len = slowik_build_audio(&buf, buf.count, 0, 0, 0, 0, 255, 0, 0, 1, frames, 2)
    var bad = buf; bad[0] = 0x41
    #expect(slowik_parse_audio(bad, len, &h) == SLOWIK_PARSE_BAD_MAGIC)
    bad = buf; bad[4] = 2
    #expect(slowik_parse_audio(bad, len, &h) == SLOWIK_PARSE_BAD_VERSION)
    bad = buf; bad[5] = 2
    #expect(slowik_parse_audio(bad, len, &h) == SLOWIK_PARSE_BAD_TYPE)
    bad = buf; bad[31] = 9
    #expect(slowik_parse_audio(bad, len, &h) == SLOWIK_PARSE_BAD_CODEC)
    #expect(slowik_parse_audio(buf, 32 + 100, &h) == SLOWIK_PARSE_NO_FRAMES)
}

@Test func controlPacketIsByteExact() {
    var buf = [UInt8](repeating: 0xAA, count: 32)
    #expect(slowik_build_control(&buf, 32, 0x0102, 0x0A0B0C0D, 1, 0, 12, 40, 20) == 32)
    let expected: [UInt8] = [0x53, 0x4C, 0x57, 0x4B, 1, 2, 0x02, 0x01, 0x0D, 0x0C, 0x0B, 0x0A, 1, 0,
                             12, 0, 40, 0, 20, 0] + [UInt8](repeating: 0, count: 12)
    #expect(buf == expected)
}

// MARK: - Jitter buffer / drift scenarios

@Test func inOrderStreamIsClean() {
    let r = sim { $0.seconds = 20 }
    #expect(r.stats.frames_concealed == 0, "\(describe(r))")
    #expect(r.underruns_after_warmup == 0)
    #expect(r.discontinuities == 0, "\(describe(r))")
    #expect(r.stats.target_ms >= 10 && r.stats.target_ms <= 15, "\(describe(r))")
    #expect(r.fill_err_max_ms < 2, "\(describe(r))")
    #expect(r.stats.link_state == SLOWIK_LINK_CONNECTED)
}

@Test func pcm24StreamPlays() {
    let r = sim { $0.seconds = 5; $0.codec = 2 }
    #expect(r.stats.frames_concealed == 0 && r.discontinuities == 0, "\(describe(r))")
    #expect(r.stats.codec == 2)
}

@Test(arguments: [0.02, 0.05])
func randomLossIsRecoveredByRedundancy(loss: Double) {
    let r = sim { $0.seconds = 60; $0.loss = loss; $0.jitter_ms = 2; $0.seed = 7 }
    // Only double losses (p^2) should need concealment.
    let lossAfter = Double(r.stats.frames_concealed) / Double(r.stats.frames_played)
    #expect(lossAfter < loss * loss * 3 + 0.0005, "\(describe(r))")
    #expect(r.stats.packets_lost_seq > 0)
    #expect(r.discontinuities == 0, "\(describe(r))")
    #expect(r.underruns_after_warmup == 0, "\(describe(r))")
}

@Test func reorderingAndDuplicatesAreHandled() {
    // 12 ms of uniform jitter on 5 ms packets reorders a lot; duplicates on top.
    let r = sim { $0.seconds = 40; $0.jitter_ms = 12; $0.dup_prob = 0.05; $0.seed = 3 }
    #expect(r.stats.frames_duplicate > 50, "\(describe(r))")
    #expect(r.concealed_after_warmup <= 2, "\(describe(r))")
    #expect(r.underruns_after_warmup <= 1, "\(describe(r))")
    #expect(r.discontinuities == 0, "\(describe(r))")
    #expect(r.stats.target_ms > 12 && r.stats.target_ms < 30, "\(describe(r))")
}

@Test func jitterBurstGrowsTargetFastThenShrinksSlowly() {
    let r = sim {
        $0.seconds = 120; $0.jitter_ms = 2; $0.seed = 11; $0.amp = 0.005 // quiet: faster shrink
        $0.burst_at_s = 20; $0.burst_len_s = 5; $0.burst_ms = 40
    }
    #expect(r.target_max_ms > 28, "\(describe(r))")              // grew during the burst
    #expect(r.stats.target_ms < r.target_max_ms - 12 && r.stats.target_ms > 10, "\(describe(r))") // and came back down later
    #expect(r.stats.link_state == SLOWIK_LINK_CONNECTED)
    #expect(r.discontinuities < 10, "\(describe(r))")             // a few concealments at burst onset only
}

@Test(arguments: [150.0, -150.0])
func clockSkewIsCompensated(ppm: Double) {
    // Long run: 10 minutes of a +/-150 ppm sender with loss and jitter.
    let r = sim { $0.seconds = 600; $0.ppm = ppm; $0.loss = 0.02; $0.jitter_ms = 8; $0.seed = 5; $0.warmup_s = 5 }
    // Sender slower (ppm > 0) -> fewer samples -> resampler must consume slower -> drift ~ -ppm.
    #expect(abs(r.drift_ppm_final + ppm) < 15, "\(describe(r))")
    #expect(r.underruns_after_warmup == 0, "\(describe(r))")
    #expect(r.fill_err_max_ms < 6, "\(describe(r))")
    #expect(r.fill_max_ms < 60, "\(describe(r))")
    #expect(r.discontinuities == 0, "\(describe(r))")
    #expect(r.stats.resyncs == 1)
}

@Test func startBurstConvergesQuickly() {
    // Wi-Fi power-save wake-up: the first 300 ms of packets arrive at once, then steady 5 ms with ~3 ms jitter.
    let r = sim { $0.seconds = 30; $0.burst_start_ms = 300; $0.jitter_ms = 3; $0.seed = 21
                  $0.warmup_s = 0.3 + 2; $0.settle_s = 0 }
    #expect(r.fill_err_max_ms < 10, "\(describe(r))")
    #expect(r.underruns_after_warmup == 0, "\(describe(r))")
    #expect(r.stats.target_ms < 25, "target poisoned by the burst: \(describe(r))")
    #expect(r.latency_max_ms < 45, "\(describe(r))")
    #expect(r.discontinuities == 0, "\(describe(r))")
}

@Test func midStreamStallFlushConvergesQuickly() {
    // 250 ms Wi-Fi stall, then everything flushed at once.
    let r = sim { $0.seconds = 30; $0.jitter_ms = 3; $0.seed = 22
                  $0.stall_at_s = 8; $0.stall_len_s = 0.25; $0.warmup_s = 8.25 + 2; $0.settle_s = 0 }
    #expect(r.fill_err_max_ms < 10, "\(describe(r))")
    #expect(r.underruns_after_warmup == 0, "\(describe(r))")
    #expect(r.stats.target_ms < 25, "target poisoned by the stall: \(describe(r))")
    #expect(r.latency_max_ms < 45, "\(describe(r))")
}

@Test func moderateJitterBurstFlushIsDrained() {
    // 120 ms stall (no link loss) then flush: excess must be dropped, not resynced.
    let r = sim { $0.seconds = 20; $0.jitter_ms = 3; $0.seed = 23
                  $0.stall_at_s = 8; $0.stall_len_s = 0.12; $0.warmup_s = 8.12 + 2; $0.settle_s = 0 }
    #expect(r.stats.resyncs == 1 && r.stats.frames_dropped > 0, "\(describe(r))")
    #expect(r.fill_err_max_ms < 10, "\(describe(r))")
    #expect(r.underruns_after_warmup == 0, "\(describe(r))")
    #expect(r.discontinuities == 0, "\(describe(r))")
}

@Test(arguments: [128, 512, 1024])
func largeRenderBlocksAndLateDeviceStartConverge(block: Int32) {
    // Device starts pulling 100 ms after packets arrive (packets pile up), in big chunks, with a start burst.
    let r = sim { $0.seconds = 30; $0.jitter_ms = 4; $0.seed = 31; $0.render_block = block
                  $0.render_start_ms = 100; $0.burst_start_ms = 300; $0.warmup_s = 3; $0.settle_s = 0 }
    #expect(r.fill_err_max_ms < 10, "\(describe(r))")
    #expect(r.underruns_after_warmup == 0, "\(describe(r))")
    #expect(r.discontinuities == 0, "\(describe(r))")
}

@Test func powerSaveBatchedArrivalsStayBounded() {
    // Wi-Fi power save: packets only delivered every 60 ms in batches. Target must cover it; no runaway fill.
    let r = sim { $0.seconds = 40; $0.jitter_ms = 2; $0.seed = 32; $0.arrival_quantum_ms = 60
                  $0.warmup_s = 5; $0.settle_s = 5 }
    #expect(r.fill_err_max_ms < 10, "\(describe(r))")
    #expect(r.underruns_after_warmup <= 1, "\(describe(r))")
    #expect(r.stats.target_ms < 80, "\(describe(r))")
}

@Test func secondSenderOnSameStreamIsIgnored() {
    let r = sim { $0.seconds = 20; $0.jitter_ms = 3; $0.seed = 33; $0.second_sender_offset_ms = 80 }
    #expect(r.stats.resyncs == 1, "\(describe(r))")
    #expect(r.stats.packets_foreign > 3000, "\(describe(r))")
    #expect(r.stats.frames_concealed == 0 && r.discontinuities == 0, "\(describe(r))")
    #expect(r.fill_err_max_ms < 3, "\(describe(r))")
}

@Test func senderSwitchAfterSilence() {
    // Locked sender goes silent (outage > 2 s); a packet from anywhere is then accepted again.
    let e = slowik_engine_create(nil)!
    defer { slowik_engine_destroy(e) }
    var buf = [UInt8](repeating: 0, count: 1024)
    let frames = [Float](repeating: 0, count: 480)
    let len = slowik_build_audio(&buf, buf.count, 0, 1, 240, 0, 50, 0, 0, 1, frames, 2)
    #expect(slowik_engine_push_packet_from(e, buf, len, 1_000_000, 1) == 0)
    #expect(slowik_engine_push_packet_from(e, buf, len, 1_100_000, 2) == Int32(SLOWIK_PUSH_FOREIGN_SENDER))
    #expect(slowik_engine_push_packet_from(e, buf, len, 3_100_001, 2) == 0)
    #expect(slowik_engine_push_packet_from(e, buf, len, 3_200_000, 1) == Int32(SLOWIK_PUSH_FOREIGN_SENDER))
}

@Test func senderRestartResyncs() {
    let r = sim { $0.seconds = 20; $0.restart_at_s = 8; $0.loss = 0.01; $0.seed = 9 }
    #expect(r.stats.resyncs == 2, "\(describe(r))")
    #expect(r.stats.link_state == SLOWIK_LINK_CONNECTED)
    #expect(r.saw_link_lost == 0, "\(describe(r))")
    #expect(r.discontinuities <= 2, "\(describe(r))")
}

@Test func seqWrapIsHarmless() {
    let r = sim { $0.seconds = 10; $0.seq_start = 0xFFFF_FF00; $0.loss = 0.02; $0.seed = 2 }
    #expect(r.stats.packets_lost_seq > 10 && r.stats.packets_lost_seq < 100, "\(describe(r))")
    #expect(r.stats.resyncs == 1)
    #expect(r.discontinuities == 0, "\(describe(r))")
}

@Test func silenceGoesLinkLostThenRecovers() {
    let r = sim { $0.seconds = 15; $0.outage_at_s = 6; $0.outage_len_s = 2; $0.seed = 4 }
    #expect(r.saw_link_lost == 1, "\(describe(r))")
    // Lost about 150 ms (+ buffer) after the outage starts, never before.
    #expect(r.link_lost_at_s > 6.1 && r.link_lost_at_s < 6.3, "lost at \(r.link_lost_at_s)")
    #expect(r.max_abs_while_lost < 1e-4, "output not silent while lost: \(r.max_abs_while_lost)")
    #expect(r.recovered_at_s > 8.0 && r.recovered_at_s < 8.1, "recovered at \(r.recovered_at_s)")
    #expect(r.stats.link_state == SLOWIK_LINK_CONNECTED)
    #expect(r.stats.link_lost_events == 1 && r.stats.resyncs == 2, "\(describe(r))")
    #expect(r.discontinuities == 0, "\(describe(r))") // fade-out and fade-in, no clicks
}

@Test func operatorMuteFadesWithoutClick() {
    let e = slowik_engine_create(nil)!
    defer { slowik_engine_destroy(e) }
    var frames = [Float](repeating: 0.5, count: 480)
    var buf = [UInt8](repeating: 0, count: 1024)
    var out = [Float](repeating: 0, count: 240)
    var maxStep: Float = 0, last: Float = 0
    for i in 0..<400 {
        for k in 0..<480 { let n = k < 240 ? i * 240 + k : (i - 1) * 240 + k - 240; frames[k] = 0.5 * sinf(Float(n) * 0.05) }
        let len = slowik_build_audio(&buf, buf.count, 0, UInt32(i), UInt64(i * 240), 0, 50, 0, 0, 1, frames, i == 0 ? 1 : 2)
        slowik_engine_push_packet(e, buf, len, UInt64(i * 5000))
        if i == 200 { slowik_engine_set_mute(e, 1) }
        slowik_engine_render(e, &out, 240)
        for x in out { maxStep = max(maxStep, abs(x - last)); last = x }
    }
    #expect(abs(last) < 1e-6)
    #expect(maxStep < 0.06)
    var ctl = [UInt8](repeating: 0, count: 32)
    slowik_engine_set_live(e, 1)
    #expect(slowik_engine_build_control(e, &ctl, 32) == 32)
    #expect(ctl[12] == 1 && ctl[13] == 1)
}
