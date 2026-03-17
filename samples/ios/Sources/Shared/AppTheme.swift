import SwiftUI

/// Design tokens for DeviceAI Labs sample app.
/// Mirrors the dark purple + cobalt palette from the KMP Compose theme.
enum AppTheme {
    /// Primary accent — cobalt blue, matches deviceai.dev brand.
    static let accent = Color(red: 0, green: 0.384, blue: 1)         // #0062FF

    /// Deep space background — top of gradient.
    static let backgroundTop = Color(red: 0.039, green: 0.067, blue: 0.125)  // #0A1120

    /// Navy surface — bottom of gradient.
    static let backgroundBottom = Color(red: 0.086, green: 0.122, blue: 0.188) // #161F30

    static let backgroundGradient = LinearGradient(
        colors: [backgroundTop, backgroundBottom],
        startPoint: .top,
        endPoint: .bottom
    )
}

// MARK: - Liquid Glass progressive enhancement (Xcode 26+ only)
// These are no-ops until the project is built with Xcode 26 + iOS 26 SDK.

extension View {
    @ViewBuilder func liquidGlassCardIfAvailable() -> some View { self }
    @ViewBuilder func liquidGlassBadgeIfAvailable() -> some View { self }
    @ViewBuilder func liquidGlassIfAvailable() -> some View { self }
    @ViewBuilder func liquidGlassBubbleIfAvailable(isUser: Bool) -> some View { self }
}
