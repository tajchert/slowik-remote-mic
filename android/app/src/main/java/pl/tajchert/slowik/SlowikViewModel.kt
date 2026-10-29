package pl.tajchert.slowik

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import pl.tajchert.slowik.audio.AudioStats
import pl.tajchert.slowik.net.NsdDiscovery
import pl.tajchert.slowik.stream.Receiver
import pl.tajchert.slowik.stream.ReceiverStore
import pl.tajchert.slowik.stream.StageState
import pl.tajchert.slowik.stream.StreamController
import pl.tajchert.slowik.stream.deriveStageState
import pl.tajchert.slowik.stream.parseReceiver

data class UiState(
    val receiver: Receiver? = null,
    val discovered: List<Receiver> = emptyList(),
    val manualText: String = "",
    val manualInvalid: Boolean = false,
    val streaming: Boolean = false,
    val stats: AudioStats = AudioStats(),
    val localMuted: Boolean = false,
    val locked: Boolean = false,
    val checks: List<CheckItem> = emptyList(),
) {
    val stage: StageState = deriveStageState(
        streaming, stats.receiverConnected, stats.live, stats.operatorMute, localMuted,
    )
}

class SlowikViewModel(app: Application) : AndroidViewModel(app) {
    private val store = ReceiverStore(app)
    private val nsd = NsdDiscovery(app)
    private val receiver = MutableStateFlow(store.load())
    private val manual = MutableStateFlow(store.load()?.label ?: "")
    private val manualInvalid = MutableStateFlow(false)
    private val locked = MutableStateFlow(false)
    private val checks = MutableStateFlow<List<CheckItem>>(emptyList())

    val ui: StateFlow<UiState> = combine(
        receiver, nsd.found, manual, manualInvalid, StreamController.running,
    ) { r, found, text, invalid, running -> UiState(r, found, text, invalid, running) }
        .combine(StreamController.stats) { s, stats -> s.copy(stats = stats) }
        .combine(StreamController.muted) { s, m -> s.copy(localMuted = m) }
        .combine(locked) { s, l -> s.copy(locked = l) }
        .combine(checks) { s, c -> s.copy(checks = c) }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), UiState())

    fun startDiscovery() = nsd.start()
    fun stopDiscovery() = nsd.stop()
    fun refreshChecks() { checks.value = checkPreflight(getApplication()) }

    fun select(r: Receiver) {
        store.save(r)
        receiver.value = r
        manual.value = r.label
        manualInvalid.value = false
    }

    fun onManualChange(text: String) {
        manual.value = text
        manualInvalid.value = false
    }

    fun applyManual() {
        val r = parseReceiver(manual.value)
        if (r == null) manualInvalid.value = true else select(r)
    }

    fun setMuted(m: Boolean) = StreamController.setMuted(m)
    fun toggleMute() = StreamController.setMuted(!StreamController.muted.value)
    fun setLocked(l: Boolean) = locked.update { l }

    override fun onCleared() = nsd.stop()
}
