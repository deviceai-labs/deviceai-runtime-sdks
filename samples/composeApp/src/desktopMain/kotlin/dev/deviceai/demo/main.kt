package dev.deviceai.demo

import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import dev.deviceai.core.DeviceAI
import dev.deviceai.core.Environment

fun main() = application {
    // Initialize SDK at startup. Switch to Environment.Production + apiKey for release builds.
    DeviceAI.initialize { environment = Environment.Development }
    Window(
        onCloseRequest = ::exitApplication,
        title = "DeviceAI",
        state = rememberWindowState(width = 480.dp, height = 760.dp)
    ) {
        App()
    }
}
