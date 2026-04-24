package dev.deviceai.core

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

internal actual fun DeviceAI.registerLifecycleFlush(context: Any?) {
    if (context == null) return
    try {
        ProcessLifecycleOwner.get().lifecycle.addObserver(object : DefaultLifecycleObserver {
            override fun onStop(owner: LifecycleOwner) {
                CoreSDKLogger.debug("DeviceAI", "app backgrounded — flushing telemetry")
                CoroutineScope(Dispatchers.Default).launch {
                    try {
                        flushTelemetry()
                    } catch (_: Exception) {}
                }
            }
        })
    } catch (_: Exception) {
        // ProcessLifecycleOwner not available (e.g. unit tests)
    }
}
