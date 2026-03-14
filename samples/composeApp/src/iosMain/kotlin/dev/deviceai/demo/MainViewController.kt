package dev.deviceai.demo

import androidx.compose.ui.window.ComposeUIViewController
import dev.deviceai.core.DeviceAI
import dev.deviceai.core.Environment
import platform.UIKit.UIViewController

// Initialised once per process — guards against SwiftUI recreating the
// UIViewControllerRepresentable (e.g. scene lifecycle / dark-mode transitions).
private val sdkInit by lazy {
    DeviceAI.initialize { environment = Environment.Development }
}

fun MainViewController(): UIViewController {
    sdkInit // ensure initialize() runs exactly once
    return ComposeUIViewController { App() }
}
