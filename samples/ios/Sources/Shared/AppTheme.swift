import SwiftUI

/// Design tokens matching the DeviceAI brand — black + amber.
enum AppTheme {
    /// Amber orange — primary accent
    static let accent = Color(red: 0.984, green: 0.659, blue: 0.118)     // #FBAA1E

    /// Pure black background
    static let background = Color.black

    /// Dark surface — cards, tab bar, nav bar
    static let surface = Color(red: 0.110, green: 0.110, blue: 0.118)    // #1C1C1E

    /// Surface variant — slightly lighter, for input fields etc.
    static let surfaceVariant = Color(red: 0.173, green: 0.173, blue: 0.180) // #2C2C2E

    /// Primary text on dark background
    static let onBackground = Color.white

    /// Secondary / muted text
    static let onBackgroundSecondary = Color(white: 0.6)

    /// Error red
    static let error = Color(red: 1, green: 0.27, blue: 0.227)           // #FF4539

    static let backgroundGradient = LinearGradient(
        colors: [background, surface],
        startPoint: .top,
        endPoint: .bottom
    )
}

// MARK: - Liquid Glass progressive enhancement (Xcode 26+ only)

extension View {
    @ViewBuilder func liquidGlassCardIfAvailable() -> some View { self }
    @ViewBuilder func liquidGlassBadgeIfAvailable() -> some View { self }
    @ViewBuilder func liquidGlassIfAvailable() -> some View { self }
    @ViewBuilder func liquidGlassBubbleIfAvailable(isUser: Bool) -> some View { self }
}
