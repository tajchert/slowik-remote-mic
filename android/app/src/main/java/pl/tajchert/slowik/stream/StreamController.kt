package pl.tajchert.slowik.stream

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import pl.tajchert.slowik.audio.AudioStats

/** Process-wide bridge between StreamingService (owner of the engine) and the UI. */
object StreamController {
    private val _stats = MutableStateFlow(AudioStats())
    private val _running = MutableStateFlow(false)
    private val _muted = MutableStateFlow(false)

    val stats: StateFlow<AudioStats> = _stats.asStateFlow()
    val running: StateFlow<Boolean> = _running.asStateFlow()
    val muted: StateFlow<Boolean> = _muted.asStateFlow()

    internal fun publishStats(s: AudioStats) { _stats.value = s }
    internal fun setRunning(r: Boolean) { _running.value = r }
    fun setMuted(m: Boolean) { _muted.value = m }
}
