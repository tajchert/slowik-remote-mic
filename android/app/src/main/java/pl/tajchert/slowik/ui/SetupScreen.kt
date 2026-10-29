package pl.tajchert.slowik.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import pl.tajchert.slowik.CheckId
import pl.tajchert.slowik.CheckItem
import pl.tajchert.slowik.UiState
import pl.tajchert.slowik.stream.Receiver

@Composable
fun SetupScreen(
    ui: UiState,
    onSelect: (Receiver) -> Unit,
    onManualChange: (String) -> Unit,
    onApplyManual: () -> Unit,
    onFix: (CheckId) -> Unit,
    onGoLive: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Slowik", fontSize = 32.sp, style = MaterialTheme.typography.headlineLarge)

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Receiver", style = MaterialTheme.typography.titleMedium)
                Text(ui.receiver?.let { "Selected: ${it.label}" } ?: "None selected")
                if (ui.discovered.isEmpty()) Text("Searching for Macs on this network...", style = MaterialTheme.typography.bodySmall)
                ui.discovered.forEach { r ->
                    OutlinedButton(onClick = { onSelect(r) }, Modifier.fillMaxWidth()) { Text("${r.name}  (${r.label})") }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = ui.manualText,
                        onValueChange = onManualChange,
                        label = { Text("IP[:port]") },
                        isError = ui.manualInvalid,
                        singleLine = true,
                        modifier = Modifier.weight(1f),
                    )
                    Button(onClick = onApplyManual, Modifier.padding(top = 8.dp)) { Text("Use") }
                }
            }
        }

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Pre-flight", style = MaterialTheme.typography.titleMedium)
                ui.checks.forEach { CheckRow(it, onFix) }
            }
        }

        Button(
            onClick = onGoLive,
            enabled = ui.receiver != null,
            modifier = Modifier.fillMaxWidth().height(88.dp),
        ) { Text("GO LIVE", fontSize = 28.sp) }
    }
}

@Composable
private fun CheckRow(c: CheckItem, onFix: (CheckId) -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(enabled = !c.ok) { onFix(c.id) }.padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(Modifier.weight(1f)) {
            Text((if (c.ok) "OK  " else "!!  ") + c.title)
            Text(c.detail, style = MaterialTheme.typography.bodySmall)
        }
        if (!c.ok && c.id != CheckId.BATTERY && c.id != CheckId.WIFI) Text("Fix", color = MaterialTheme.colorScheme.primary)
    }
}
