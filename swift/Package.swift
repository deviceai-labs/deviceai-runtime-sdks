// swift-tools-version: 5.9
//
// DeviceAI Swift SDK
// ──────────────────
// Three opt-in products. Add only what your app needs:
//
//   // LLM inference (llama.cpp)
//   .product(name: "DeviceAiLlm", package: "DeviceAI")
//
//   // Speech-to-text (whisper.cpp)
//   .product(name: "DeviceAiStt", package: "DeviceAI")
//
//   // Text-to-speech (sherpa-onnx / Piper)
//   .product(name: "DeviceAiTts", package: "DeviceAI")
//
// DeviceAiCore is an internal shared foundation — never imported directly by app code.
// The DeviceAI entry point lives in Core; feature modules extend it via Swift extensions.
//
// ── Binary targets ───────────────────────────────────────────────────────────
// Pre-compiled XCFrameworks (CLlama, CWhisper, CSherpaOnnx) are built separately
// from the C++ submodules in Binaries/. Until they are built, all engines operate
// in stub mode and the SDK compiles + tests pass without any native code.
//
// To enable native inference:
//   1. Run: scripts/build-xcframeworks.sh
//   2. Uncomment the binary targets and add them back as dependencies.
//
// See: swift/Binaries/README.md

import PackageDescription

let package = Package(
    name: "DeviceAI",
    platforms: [
        .iOS(.v17),    // matches Kotlin KMP target; covers ~95% of active devices
        .macOS(.v14),  // desktop + Xcode previews
    ],
    products: [
        // LLM: text generation + RAG
        .library(name: "DeviceAiLlm", targets: ["DeviceAiCore", "DeviceAiLlm"]),

        // STT: speech-to-text via whisper.cpp
        .library(name: "DeviceAiStt", targets: ["DeviceAiCore", "DeviceAiStt"]),

        // TTS: text-to-speech via sherpa-onnx (Piper / Kokoro)
        .library(name: "DeviceAiTts", targets: ["DeviceAiCore", "DeviceAiTts"]),
    ],
    targets: [

        // ── DeviceAiCore (source) ─────────────────────────────────────────────
        // Owns the DeviceAI entry point. All shared foundations.
        // Zero native dependencies — pure Swift.
        .target(
            name: "DeviceAiCore",
            path: "Sources/DeviceAiCore",
            swiftSettings: strictConcurrency
        ),

        // ── DeviceAiLlm (source) ──────────────────────────────────────────────
        // Extends DeviceAI with .llm. Wraps CLlama binary with idiomatic Swift API.
        // Binary dep: add "CLlama" after running scripts/build-xcframeworks.sh
        .target(
            name: "DeviceAiLlm",
            dependencies: ["DeviceAiCore", "CLlama"],
            path: "Sources/DeviceAiLlm",
            swiftSettings: strictConcurrency
        ),

        // ── DeviceAiStt (source) ──────────────────────────────────────────────
        // Extends DeviceAI with .stt. Wraps CWhisper binary with idiomatic Swift API.
        // Binary dep: add "CWhisper" after running scripts/build-xcframeworks.sh
        .target(
            name: "DeviceAiStt",
            dependencies: ["DeviceAiCore", "CWhisper"],
            path: "Sources/DeviceAiStt",
            swiftSettings: strictConcurrency
        ),

        // ── DeviceAiTts (source) ──────────────────────────────────────────────
        // Extends DeviceAI with .tts. Wraps CSherpaOnnx binary with idiomatic Swift API.
        // Binary dep: add "CSherpaOnnx" after running scripts/build-xcframeworks.sh
        .target(
            name: "DeviceAiTts",
            dependencies: ["DeviceAiCore"],
            path: "Sources/DeviceAiTts",
            swiftSettings: strictConcurrency
        ),

        // ── XCFramework binary targets ────────────────────────────────────────
        .binaryTarget(name: "CLlama",      path: "Binaries/CLlama.xcframework"),
        .binaryTarget(name: "CWhisper",    path: "Binaries/CWhisper.xcframework"),
        // .binaryTarget(name: "CSherpaOnnx", path: "Binaries/CSherpaOnnx.xcframework"),

        // ── Tests ─────────────────────────────────────────────────────────────
        // All tests use stub engines — no binary targets needed in test builds.
        .testTarget(
            name: "DeviceAiCoreTests",
            dependencies: ["DeviceAiCore"],
            path: "Tests/DeviceAiCoreTests"
        ),
        .testTarget(
            name: "DeviceAiLlmTests",
            dependencies: ["DeviceAiCore", "DeviceAiLlm"],
            path: "Tests/DeviceAiLlmTests"
        ),
        .testTarget(
            name: "DeviceAiSttTests",
            dependencies: ["DeviceAiCore", "DeviceAiStt"],
            path: "Tests/DeviceAiSttTests"
        ),
        .testTarget(
            name: "DeviceAiTtsTests",
            dependencies: ["DeviceAiCore", "DeviceAiTts"],
            path: "Tests/DeviceAiTtsTests"
        ),
    ]
)

// Swift 6 strict concurrency — opt in now, required later.
// Catches data races at compile time across all targets.
private let strictConcurrency: [SwiftSetting] = [
    .enableExperimentalFeature("StrictConcurrency"),
]
