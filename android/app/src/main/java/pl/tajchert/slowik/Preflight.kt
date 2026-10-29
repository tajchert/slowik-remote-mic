package pl.tajchert.slowik

import android.Manifest
import android.app.NotificationManager
import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.BatteryManager
import android.os.PowerManager
import androidx.core.content.ContextCompat

enum class CheckId { MIC, DND, BATTERY, WIFI, BATTERY_OPT }

data class CheckItem(val id: CheckId, val title: String, val ok: Boolean, val detail: String)

/** Pure evaluation so it can be unit tested without Android. */
fun evaluatePreflight(
    micGranted: Boolean,
    dndOn: Boolean,
    batteryPct: Int,
    onWifi: Boolean,
    batteryOptExempt: Boolean,
): List<CheckItem> = listOf(
    CheckItem(CheckId.MIC, "Microphone permission", micGranted, if (micGranted) "granted" else "needed to stream"),
    CheckItem(CheckId.DND, "Do Not Disturb on", dndOn, if (dndOn) "on" else "calls and notifications may interrupt"),
    CheckItem(CheckId.BATTERY, "Battery above 50%", batteryPct > 50, "$batteryPct%"),
    CheckItem(CheckId.WIFI, "Connected to Wi-Fi", onWifi, if (onWifi) "yes" else "not on Wi-Fi"),
    CheckItem(
        CheckId.BATTERY_OPT, "Battery optimisation off", batteryOptExempt,
        if (batteryOptExempt) "exempt" else "Android may throttle the stream",
    ),
)

fun checkPreflight(context: Context): List<CheckItem> {
    val mic = ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
        PackageManager.PERMISSION_GRANTED
    val nm = context.getSystemService(NotificationManager::class.java)
    val dnd = nm.currentInterruptionFilter.let {
        it != NotificationManager.INTERRUPTION_FILTER_ALL && it != NotificationManager.INTERRUPTION_FILTER_UNKNOWN
    }
    val bm = context.getSystemService(BatteryManager::class.java)
    val pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
    val cm = context.getSystemService(ConnectivityManager::class.java)
    val wifi = cm.getNetworkCapabilities(cm.activeNetwork)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
    val pm = context.getSystemService(PowerManager::class.java)
    return evaluatePreflight(mic, dnd, pct, wifi, pm.isIgnoringBatteryOptimizations(context.packageName))
}
