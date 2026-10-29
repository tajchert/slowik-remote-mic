package pl.tajchert.slowik.audio

import android.content.Context
import android.media.AudioManager

/** Snapshot of the native engine, polled from the UI/service. */
data class AudioStats(
    val inputPeak: Float = 0f,
    val inputRms: Float = 0f,
    val packetsSent: Long = 0,
    /** CONTROL packet received within the last second. */
    val receiverConnected: Boolean = false,
    val live: Boolean = false,
    val operatorMute: Boolean = false,
    val lossPermille: Int = 0,
    val latencyMs: Int = 0,
    val jbMs: Int = 0,
    val streamOpen: Boolean = false,
    val overruns: Long = 0,
    val grantedConfig: String = "not started",
)

/** Kotlin facade over the Oboe capture + UDP sender (see cpp/engine.cpp). */
class SlowikAudioEngine(context: Context) {
    private val unprocessed: Boolean =
        (context.getSystemService(Context.AUDIO_SERVICE) as AudioManager)
            .getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"

    private var handle: Long = nativeCreate()

    @Synchronized
    fun start(host: String, port: Int): Boolean = handle != 0L && nativeStart(handle, host, port, unprocessed)

    @Synchronized
    fun stop() {
        if (handle != 0L) nativeStop(handle)
    }

    @Synchronized
    fun setMuted(muted: Boolean) {
        if (handle != 0L) nativeSetMuted(handle, muted)
    }

    /** battery 0-100 (255 unknown), rssi dBm (0 unknown). */
    @Synchronized
    fun setDeviceStatus(battery: Int, rssi: Int, charging: Boolean) {
        if (handle != 0L) nativeSetDeviceStatus(handle, battery, rssi, charging)
    }

    @Synchronized
    fun stats(): AudioStats {
        if (handle == 0L) return AudioStats()
        val v = nativeStats(handle)
        return AudioStats(
            inputPeak = v[0].toFloat(),
            inputRms = v[1].toFloat(),
            packetsSent = v[2].toLong(),
            receiverConnected = v[3] != 0.0,
            live = v[4] != 0.0,
            operatorMute = v[5] != 0.0,
            lossPermille = v[6].toInt(),
            latencyMs = v[7].toInt(),
            jbMs = v[8].toInt(),
            streamOpen = v[15] != 0.0,
            overruns = v[16].toLong(),
            grantedConfig = describe(v),
        )
    }

    @Synchronized
    fun release() {
        if (handle != 0L) {
            nativeStop(handle)
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private fun describe(v: DoubleArray): String {
        if (v[9] == 0.0) return "no stream"
        // Oboe enums: SharingMode Exclusive=0 Shared=1; InputPreset Generic=1 Camcorder=5 VoiceRecognition=6
        // VoiceCommunication=7 Unprocessed=9 VoicePerformance=10; PerformanceMode None=10 PowerSaving=11 LowLatency=12
        val sharing = if (v[10].toInt() == 0) "exclusive" else "shared"
        val preset = when (v[11].toInt()) {
            9 -> "unprocessed"; 10 -> "voice-performance"; 7 -> "voice-communication"; else -> "preset${v[11].toInt()}"
        }
        val perf = when (v[14].toInt()) { 12 -> "low-latency"; 11 -> "power-saving"; else -> "default" }
        return "${v[9].toInt()} Hz, $sharing, $preset, $perf, burst ${v[12].toInt()}, cb ${v[13].toInt()} frames"
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(h: Long)
    private external fun nativeStart(h: Long, host: String, port: Int, unprocessed: Boolean): Boolean
    private external fun nativeStop(h: Long)
    private external fun nativeSetMuted(h: Long, m: Boolean)
    private external fun nativeSetDeviceStatus(h: Long, battery: Int, rssi: Int, charging: Boolean)
    private external fun nativeStats(h: Long): DoubleArray

    companion object {
        init {
            System.loadLibrary("slowik_audio")
        }
    }
}
