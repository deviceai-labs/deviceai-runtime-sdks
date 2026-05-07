// swift-tools-version: 6.0
import PackageDescription

let version = "0.0.1"

let package = Package(
    name: "DeviceAI",
    platforms: [
        .iOS(.v17),
        .macOS(.v14),
    ],
    products: [
        .library(name: "DeviceAI", targets: ["DeviceAI"]),
        .library(name: "DeviceAISpeech", targets: ["DeviceAISpeech"]),
        .library(name: "DeviceAILLM", targets: ["DeviceAILLM"]),
    ],
    targets: [
        // ── Pre-built C++ engines as XCFrameworks ────────────────────
        .binaryTarget(name: "CWhisper", path: "swift/Binaries/CWhisper.xcframework"),
        .binaryTarget(name: "CLlama", path: "swift/Binaries/CLlama.xcframework"),
        .binaryTarget(name: "CSherpaOnnx", path: "swift/Binaries/CSherpaOnnx.xcframework"),

        // ── C interop module (compiles features layer C++) ────────────
        .target(
            name: "CDeviceAI",
            dependencies: ["CWhisper", "CLlama", "CSherpaOnnx"],
            path: "swift/Sources/CDeviceAI",
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("include"),
            ],
            linkerSettings: [
                .linkedFramework("Accelerate"),
                .linkedFramework("Metal"),
                .linkedFramework("MetalKit"),
                .linkedFramework("CoreML"),
                .linkedLibrary("bz2"),
            ]
        ),

        // ── Core module (entry point + cloud + telemetry) ────────────
        .target(
            name: "DeviceAI",
            path: "swift/Sources/DeviceAI"
        ),

        // ── Speech module (STT + TTS) ────────────────────────────────
        .target(
            name: "DeviceAISpeech",
            dependencies: ["DeviceAI", "CDeviceAI"],
            path: "swift/Sources/DeviceAISpeech"
        ),

        // ── LLM module (chat + RAG) ─────────────────────────────────
        .target(
            name: "DeviceAILLM",
            dependencies: ["DeviceAI", "CDeviceAI"],
            path: "swift/Sources/DeviceAILLM"
        ),

        // ── Tests ────────────────────────────────────────────────────
        .testTarget(
            name: "DeviceAITests",
            dependencies: ["DeviceAI"],
            path: "swift/Tests/DeviceAITests"
        ),
        .testTarget(
            name: "DeviceAISpeechTests",
            dependencies: ["DeviceAISpeech"],
            path: "swift/Tests/DeviceAISpeechTests"
        ),
        .testTarget(
            name: "DeviceAILLMTests",
            dependencies: ["DeviceAILLM"],
            path: "swift/Tests/DeviceAILLMTests"
        ),
    ]
)
