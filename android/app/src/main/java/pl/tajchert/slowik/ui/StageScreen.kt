package pl.tajchert.slowik.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import pl.tajchert.slowik.UiState
import pl.tajchert.slowik.stream.StageState

fun stageColor(s: StageState): Color = when (s) {
    StageState.NO_RECEIVER -> Color(0xFF424242)
    StageState.READY -> Color(0xFFFFB300)
    StageState.LIVE -> Color(0xFFD50000)
    StageState.MUTED -> Color(0xFF1565C0)
}

@Composable
fun StageScreen(
    ui: UiState,
    onToggleMute: () -> Unit,
    onLock: () -> Unit,
    onUnlock: () -> Unit,
    onStop: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val bg = stageColor(ui.stage)
    val fg = if (ui.stage == StageState.READY) Color.Black else Color.White
    Box(modifier.fillMaxSize().background(bg)) {
        Column(
            Modifier.fillMaxSize().padding(16.dp)
                // long-press anywhere toggles mute; plain taps do nothing
                .pointerInput(Unit) { detectTapGestures(onLongPress = { onToggleMute() }) },
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                "Hold anywhere to ${if (ui.localMuted) "UNMUTE" else "MUTE"}",
                color = fg.copy(alpha = 0.8f), fontSize = 16.sp,
            )
            Text(
                ui.stage.label, color = fg, fontSize = 64.sp, fontWeight = FontWeight.Black,
                textAlign = TextAlign.Center, lineHeight = 70.sp,
            )
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp)) {
                LevelMeter(ui.stats.inputPeak, ui.stats.inputRms, fg)
                val s = ui.stats
                Text(
                    "loss ${"%.1f".format(s.lossPermille / 10f)}%   latency ${if (s.latencyMs == 0) "?" else "${s.latencyMs} ms"}   jb ${s.jbMs} ms",
                    color = fg, fontSize = 14.sp,
                )
                Text("${s.packetsSent} pkts  |  ${s.grantedConfig}", color = fg.copy(alpha = 0.7f), fontSize = 11.sp)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    HoldButton("HOLD: LOCK", Modifier.weight(1f), onLock)
                    HoldButton("HOLD: STOP", Modifier.weight(1f), onStop)
                }
            }
        }
        if (ui.locked) LockOverlay(onUnlock)
    }
}

@Composable
private fun HoldButton(text: String, modifier: Modifier, onHold: () -> Unit) {
    Box(
        modifier.height(56.dp).clip(CircleShape).background(Color.Black.copy(alpha = 0.35f))
            .pointerInput(Unit) { detectTapGestures(onLongPress = { onHold() }) },
        contentAlignment = Alignment.Center,
    ) { Text(text, color = Color.White, fontSize = 14.sp) }
}

@Composable
private fun LevelMeter(peak: Float, rms: Float, fg: Color) {
    Box(Modifier.fillMaxWidth().height(20.dp).clip(CircleShape).background(Color.Black.copy(alpha = 0.3f))) {
        Box(Modifier.fillMaxWidth(rms.coerceIn(0f, 1f).let { it.toDouble().let { v -> Math.sqrt(v).toFloat() } }).height(20.dp).background(fg.copy(alpha = 0.5f)))
        Box(
            Modifier.fillMaxWidth(Math.sqrt(peak.coerceIn(0f, 1f).toDouble()).toFloat()).height(20.dp)
                .background(if (peak > 0.95f) Color.Red else fg.copy(alpha = 0.9f)),
        )
    }
}

/** Swallows all touches; only a long-press on the unlock target releases it. */
@Composable
private fun LockOverlay(onUnlock: () -> Unit) {
    BackHandler(enabled = true) {}
    Box(
        Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.55f))
            .pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.BottomCenter,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(24.dp)) {
            Text("LOCKED", color = Color.White, fontSize = 20.sp)
            Box(
                Modifier.padding(top = 12.dp).size(96.dp).clip(CircleShape).background(Color.White.copy(alpha = 0.25f))
                    .pointerInput(Unit) { detectTapGestures(onLongPress = { onUnlock() }) },
                contentAlignment = Alignment.Center,
            ) { Text("HOLD\nTO UNLOCK", color = Color.White, fontSize = 12.sp, textAlign = TextAlign.Center) }
        }
    }
}
