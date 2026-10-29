package pl.tajchert.slowik.net

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import pl.tajchert.slowik.stream.Receiver

/** Browses `_slowik._udp.` and resolves each hit to host:port (resolves are serialised). */
@Suppress("DEPRECATION")
class NsdDiscovery(context: Context) {
    private val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val _found = MutableStateFlow<List<Receiver>>(emptyList())
    val found: StateFlow<List<Receiver>> = _found.asStateFlow()

    private var listener: NsdManager.DiscoveryListener? = null
    private val queue = ArrayDeque<NsdServiceInfo>()
    private var resolving = false

    @Synchronized
    fun start() {
        if (listener != null) return
        val l = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) { listener = null }
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
            override fun onDiscoveryStopped(serviceType: String) {}
            override fun onServiceFound(info: NsdServiceInfo) { enqueue(info) }
            override fun onServiceLost(info: NsdServiceInfo) {
                _found.update { list -> list.filterNot { it.name == info.serviceName } }
            }
        }
        listener = l
        runCatching { nsd.discoverServices("_slowik._udp.", NsdManager.PROTOCOL_DNS_SD, l) }
            .onFailure { listener = null }
    }

    @Synchronized
    fun stop() {
        listener?.let { runCatching { nsd.stopServiceDiscovery(it) } }
        listener = null
        queue.clear()
        resolving = false
    }

    @Synchronized
    private fun enqueue(info: NsdServiceInfo) {
        queue.addLast(info)
        next()
    }

    @Synchronized
    private fun next() {
        if (resolving) return
        val info = queue.removeFirstOrNull() ?: return
        resolving = true
        nsd.resolveService(info, object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) { done() }
            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                val host = serviceInfo.host?.hostAddress
                if (host != null) {
                    val r = Receiver(serviceInfo.serviceName, host, serviceInfo.port)
                    _found.update { list -> list.filterNot { it.name == r.name } + r }
                }
                done()
            }
        })
    }

    @Synchronized
    private fun done() {
        resolving = false
        next()
    }
}
