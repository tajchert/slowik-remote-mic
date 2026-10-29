package pl.tajchert.slowik

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import pl.tajchert.slowik.core.designsystem.theme.SlowikTheme
import pl.tajchert.slowik.stream.StreamingService
import pl.tajchert.slowik.stream.parseReceiver
import pl.tajchert.slowik.ui.SetupScreen
import pl.tajchert.slowik.ui.StageScreen

class MainActivity : ComponentActivity() {
    private val vm: SlowikViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handleDebugIntent(intent)
        setContent {
            SlowikTheme {
                val ui by vm.ui.collectAsStateWithLifecycle()
                val uiNow by rememberUpdatedState(ui)

                val permLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
                    vm.refreshChecks()
                    if (it[Manifest.permission.RECORD_AUDIO] == true) uiNow.receiver?.let { r -> StreamingService.start(this, r) }
                }

                // Keep the screen on and dim it while streaming.
                DisposableEffect(ui.streaming) {
                    if (ui.streaming) {
                        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        window.attributes = window.attributes.apply { screenBrightness = 0.1f }
                    }
                    onDispose {
                        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        window.attributes = window.attributes.apply {
                            screenBrightness = WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE
                        }
                    }
                }

                if (ui.streaming) {
                    StageScreen(
                        ui,
                        onToggleMute = vm::toggleMute,
                        onLock = { vm.setLocked(true) },
                        onUnlock = { vm.setLocked(false) },
                        onStop = { vm.setLocked(false); StreamingService.stop(this) },
                        modifier = Modifier.fillMaxSize().safeDrawingPadding(),
                    )
                } else {
                    SetupScreen(
                        ui,
                        onSelect = vm::select,
                        onManualChange = vm::onManualChange,
                        onApplyManual = vm::applyManual,
                        onFix = ::fix,
                        onGoLive = {
                            val perms = buildList {
                                add(Manifest.permission.RECORD_AUDIO)
                                if (Build.VERSION.SDK_INT >= 33) add(Manifest.permission.POST_NOTIFICATIONS)
                            }
                            permLauncher.launch(perms.toTypedArray())
                        },
                        modifier = Modifier.fillMaxSize().safeDrawingPadding(),
                    )
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleDebugIntent(intent)
    }

    override fun onStart() {
        super.onStart()
        vm.startDiscovery()
    }

    override fun onResume() {
        super.onResume()
        vm.refreshChecks()
    }

    override fun onStop() {
        vm.stopDiscovery()
        super.onStop()
    }

    private fun fix(id: CheckId) {
        when (id) {
            CheckId.MIC -> requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1)
            CheckId.DND -> startActivity(Intent(Settings.ACTION_NOTIFICATION_POLICY_ACCESS_SETTINGS))
            CheckId.BATTERY_OPT -> startActivity(
                Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")),
            )
            else -> {}
        }
    }

    /** Debug builds only: `am start -n ... --es autostart 10.0.2.2:47800` selects a receiver and goes live. */
    private fun handleDebugIntent(i: Intent?) {
        if (!BuildConfig.DEBUG) return
        val r = i?.getStringExtra("autostart")?.let(::parseReceiver) ?: return
        vm.select(r)
        StreamingService.start(this, r)
    }
}
