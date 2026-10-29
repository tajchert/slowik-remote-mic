package pl.tajchert.slowik.stream

import android.content.Context

const val DEFAULT_PORT = 47800

data class Receiver(val name: String, val host: String, val port: Int = DEFAULT_PORT) {
    val label: String get() = "$host:$port"
}

/** Parses "IP", "IP:port", "host:port" or "slowik://IP:port". Returns null if invalid. */
fun parseReceiver(input: String): Receiver? {
    var s = input.trim().removePrefix("slowik://").trimEnd('/')
    if (s.isEmpty()) return null
    var port = DEFAULT_PORT
    val idx = s.lastIndexOf(':')
    if (idx >= 0) {
        port = s.substring(idx + 1).toIntOrNull() ?: return null
        s = s.substring(0, idx)
    }
    if (port !in 1..65535) return null
    if (s.isEmpty() || s.any { it.isWhitespace() || it == '/' }) return null
    return Receiver(name = s, host = s, port = port)
}

class ReceiverStore(context: Context) {
    private val prefs = context.getSharedPreferences("receiver", Context.MODE_PRIVATE)

    fun load(): Receiver? {
        val host = prefs.getString("host", null) ?: return null
        return Receiver(prefs.getString("name", host) ?: host, host, prefs.getInt("port", DEFAULT_PORT))
    }

    fun save(r: Receiver) {
        prefs.edit().putString("host", r.host).putInt("port", r.port).putString("name", r.name).apply()
    }
}
