package pl.tajchert.slowik.stream

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiInfo
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import pl.tajchert.slowik.MainActivity
import pl.tajchert.slowik.R
import pl.tajchert.slowik.audio.SlowikAudioEngine

/** Foreground (microphone) service that owns the audio engine while streaming. */
class StreamingService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var engine: SlowikAudioEngine? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var loop: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            shutdown()
            return START_NOT_STICKY
        }
        val host = intent?.getStringExtra(EXTRA_HOST)
        val port = intent?.getIntExtra(EXTRA_PORT, DEFAULT_PORT) ?: DEFAULT_PORT
        if (host == null) {
            stopSelf()
            return START_NOT_STICKY
        }
        startInForeground(host, port)
        if (engine == null) begin(host, port)
        return START_NOT_STICKY
    }

    private fun startInForeground(host: String, port: Int) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, getString(R.string.app_name), NotificationManager.IMPORTANCE_LOW),
        )
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE,
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, StreamingService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE,
        )
        val n: Notification = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentTitle("Slowik streaming")
            .setContentText("to $host:$port")
            .setOngoing(true)
            .setContentIntent(open)
            .addAction(0, "Stop", stop)
            .build()
        startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
    }

    private fun begin(host: String, port: Int) {
        val wifi = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "slowik:wifi").apply {
            setReferenceCounted(false)
            acquire()
        }
        val pm = getSystemService(PowerManager::class.java)
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "slowik:stream").apply {
            setReferenceCounted(false)
            acquire()
        }
        val e = SlowikAudioEngine(this)
        engine = e
        e.setMuted(StreamController.muted.value)
        e.start(host, port)
        StreamController.setRunning(true)
        loop = scope.launch {
            var tick = 0
            while (true) {
                e.setMuted(StreamController.muted.value)
                if (tick % 10 == 0) pushDeviceStatus(e)
                StreamController.publishStats(e.stats())
                tick++
                delay(100)
            }
        }
    }

    private fun pushDeviceStatus(e: SlowikAudioEngine) {
        val bat = registerReceiver(null, android.content.IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val level = bat?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = bat?.getIntExtra(BatteryManager.EXTRA_SCALE, 100) ?: 100
        val pct = if (level >= 0 && scale > 0) level * 100 / scale else 255
        val charging = (bat?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0) != 0
        e.setDeviceStatus(pct, wifiRssi(this), charging)
    }

    private fun shutdown() {
        loop?.cancel()
        engine?.release()
        engine = null
        wifiLock?.takeIf { it.isHeld }?.release()
        wakeLock?.takeIf { it.isHeld }?.release()
        StreamController.setRunning(false)
        StreamController.publishStats(pl.tajchert.slowik.audio.AudioStats())
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        if (engine != null) shutdown()
        scope.cancel()
        super.onDestroy()
    }

    companion object {
        const val ACTION_STOP = "pl.tajchert.slowik.STOP"
        const val EXTRA_HOST = "host"
        const val EXTRA_PORT = "port"
        private const val CHANNEL = "stream"
        private const val NOTIF_ID = 1

        fun start(context: Context, r: Receiver) {
            context.startForegroundService(
                Intent(context, StreamingService::class.java).putExtra(EXTRA_HOST, r.host).putExtra(EXTRA_PORT, r.port),
            )
        }

        fun stop(context: Context) {
            context.startService(Intent(context, StreamingService::class.java).setAction(ACTION_STOP))
        }
    }
}

/** Wi-Fi RSSI in dBm of the active network, or 0 if not on Wi-Fi / unknown. */
fun wifiRssi(context: Context): Int {
    val cm = context.getSystemService(ConnectivityManager::class.java)
    val caps = cm.getNetworkCapabilities(cm.activeNetwork) ?: return 0
    if (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return 0
    val rssi = (caps.transportInfo as? WifiInfo)?.rssi ?: return 0
    return if (rssi in -127..-1) rssi else 0
}
