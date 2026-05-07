# DeviceAI

**On-device AI for Android & iOS — speech recognition, text-to-speech, and LLM chat. Zero cloud latency, zero privacy risk. Optional cloud backend for OTA model updates, telemetry, and device management.**

[![Build](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml/badge.svg)](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)
[![Maven Central](https://img.shields.io/maven-central/v/dev.deviceai/core)](https://central.sonatype.com/artifact/dev.deviceai/core)
[![Kotlin](https://img.shields.io/badge/Kotlin-2.2-blueviolet?logo=kotlin)](https://kotlinlang.org)
[![Swift](https://img.shields.io/badge/Swift-6.0-orange?logo=swift)](https://swift.org)
[![Android](https://img.shields.io/badge/Platform-Android-green)](https://developer.android.com)
[![iOS](https://img.shields.io/badge/Platform-iOS%2017%2B-blue)](https://developer.apple.com)

---

## Install

### Android (Kotlin)

```kotlin
// build.gradle.kts
implementation("dev.deviceai:core:0.3.0-alpha01")
implementation("dev.deviceai:speech:0.3.0-alpha01")   // STT + TTS
implementation("dev.deviceai:llm:0.3.0-alpha01")      // LLM + RAG
```

### iOS / macOS (Swift Package Manager)

Add the DeviceAI package to your Xcode project or `Package.swift`:

```swift
// Package.swift
dependencies: [
    .package(url: "https://github.com/deviceai-labs/deviceai", from: "0.0.1")
]
```

Then add the modules you need:

```swift
.target(
    name: "YourApp",
    dependencies: [
        .product(name: "DeviceAI", package: "deviceai"),
        .product(name: "DeviceAISpeech", package: "deviceai"),   // STT + TTS
        .product(name: "DeviceAILLM", package: "deviceai"),      // LLM + RAG
    ]
)
```

Or in Xcode: **File → Add Package Dependencies** → paste `https://github.com/deviceai-labs/deviceai` → select the modules you need.

---

## Initialize

### Android

```kotlin
class MyApp : Application() {
    override fun onCreate() {
        super.onCreate()
        PlatformStorage.initialize(this)
        DeviceAI.initialize(context = this)
    }
}
```

### iOS / macOS

```swift
import DeviceAI

// Local mode — no cloud, fully offline
DeviceAI.initialize()

// With cloud backend (optional)
DeviceAI.initialize(apiKey: "dai_live_...") {
    $0.telemetry = .minimal
}
```

That's it. The SDK runs fully on-device with no backend required.

### With cloud backend (optional)

**Android:**

```kotlin
DeviceAI.initialize(context = this, apiKey = "dai_live_...") {
    telemetry = TelemetryLevel.Minimal
    appVersion = BuildConfig.VERSION_NAME
}
```

**iOS:**

```swift
DeviceAI.initialize(apiKey: "dai_live_...") {
    $0.telemetry = .minimal
    $0.appVersion = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String
}
```

The API key connects the SDK to the DeviceAI cloud backend. Device hardware (RAM, CPU, SoC) is detected automatically — no manual configuration needed.

---

## Speech-to-Text

### Android

```kotlin
SpeechBridge.initStt(modelPath, SttConfig(language = "en", useGpu = true))

// From raw audio samples
val text = SpeechBridge.transcribeAudio(samples)  // FloatArray, 16kHz mono

// From a WAV file
val textFromFile = SpeechBridge.transcribe("/path/to/audio.wav")

SpeechBridge.shutdownStt()
```

### iOS

```swift
let engine = try await SttEngine(modelPath: path, config: .init(language: "en"))

// From raw audio samples
let text = try await engine.transcribe(samples: audioBuffer)  // [Float], 16kHz mono

// From a WAV file
let textFromFile = try await engine.transcribe(audioPath: "/path/to/audio.wav")

engine.shutdown()
```

Powered by [whisper.cpp](https://github.com/ggerganov/whisper.cpp). Runs 7× faster than real-time on mid-range hardware.

## Text-to-Speech

### Android

```kotlin
SpeechBridge.initTts(modelPath, tokensPath, TtsConfig(speechRate = 1.0f))

val pcm: ShortArray = SpeechBridge.synthesize("Hello from DeviceAI.")
// Play with AudioTrack

SpeechBridge.shutdownTts()
```

### iOS

```swift
let tts = try await TtsEngine(modelPath: path, tokensPath: tokens)
let audio = try await tts.synthesize("Hello from DeviceAI")
tts.shutdown()

// Or use Apple's built-in voices (zero setup, no model download):
let systemTts = SystemTTSEngine()
try await systemTts.speak("Hello from DeviceAI")
```

Powered by [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx). Supports VITS and Kokoro voice models.

## LLM Chat

### Android

```kotlin
val session = DeviceAI.llm.chat("/path/to/model.gguf") {
    systemPrompt = "You are a helpful assistant."
    maxTokens = 512
    temperature = 0.7f
    useGpu = true
}

// Streaming (recommended for UI)
session.send("What is Kotlin?").collect { token -> print(token) }

// Multi-turn — history managed automatically
session.send("Give me an example.").collect { print(it) }

// Lifecycle
session.cancel()        // abort generation
session.clearHistory()  // fresh conversation
session.close()         // unload model
```

### iOS

```swift
let session = try await ChatSession(modelPath: path) {
    $0.systemPrompt = "You are a helpful assistant."
    $0.maxTokens = 512
    $0.temperature = 0.7
}

// Streaming
for try await token in try session.send("What is Swift?") {
    print(token, terminator: "")
}

// Multi-turn — history managed automatically
for try await token in try session.send("Give me an example.") {
    print(token, terminator: "")
}

// Lifecycle
session.cancel()        // abort generation
session.clearHistory()  // fresh conversation
session.close()         // unload model
```

Powered by [llama.cpp](https://github.com/ggerganov/llama.cpp). Supports any GGUF model. Vulkan GPU on Android, Metal GPU on iOS.

## Offline RAG

### Android

```kotlin
val store = BM25RagStore(rawChunks = listOf(
    "DeviceAI supports Android and iOS.",
    "LLM inference uses llama.cpp with Vulkan GPU."
))
val session = DeviceAI.llm.chat("/path/to/model.gguf") { ragStore = store }
session.send("What GPU does DeviceAI use?").collect { print(it) }
```

### iOS

```swift
let store = BM25RagStore(chunks: [
    "DeviceAI supports Android and iOS.",
    "LLM inference uses llama.cpp with Metal GPU."
])
let session = try await ChatSession(modelPath: path) {
    $0.ragStore = store
}
for try await token in try session.send("What GPU does DeviceAI use?") {
    print(token, terminator: "")
}
```

No embedding model needed — BM25 keyword retrieval runs entirely on-device.

---

## Telemetry

When telemetry is enabled, the SDK automatically tracks performance metrics for all modules:

### What's collected

| Module | Metrics |
|--------|---------|
| **STT** | Model load time, transcription latency, audio duration (input_length_ms) |
| **TTS** | Model load time, synthesis latency, text length (output_chars) |
| **LLM** | Model load time, inference latency, time-to-first-token, tokens/sec, token counts, finish reason |

### What's NEVER collected

- Prompt or response text content
- Audio recordings or transcript content
- PII by default

> Apps should avoid putting PII in `appAttributes`, since developer-provided attributes are sent in the capability profile.

### Telemetry levels

**Android:**

```kotlin
DeviceAI.initialize(context = this, apiKey = "dai_live_...") {
    telemetry = TelemetryLevel.Off      // default — nothing sent
    telemetry = TelemetryLevel.Minimal  // model load/unload + inference metrics
    telemetry = TelemetryLevel.Full     // includes OTA downloads + manifest syncs
}
```

**iOS:**

```swift
DeviceAI.initialize(apiKey: "dai_live_...") {
    $0.telemetry = .off      // default — nothing sent
    $0.telemetry = .minimal  // model load/unload + inference metrics
    $0.telemetry = .full     // includes OTA downloads + manifest syncs
}
```

Events are batched on-device and delivered efficiently — respects Wi-Fi preference, data-saver mode, and flushes automatically when the app goes to background.

### Custom telemetry sink

Route events to your own analytics instead of the DeviceAI backend:

**Android:**

```kotlin
DeviceAI.initialize(context = this, apiKey = "dai_live_...") {
    telemetry = TelemetryLevel.Minimal
    telemetrySink = object : TelemetrySink {
        override suspend fun ingest(events: List<TelemetryEvent>) {
            myAnalytics.track(events)
        }
    }
}
```

**iOS:**

```swift
DeviceAI.initialize(apiKey: "dai_live_...") {
    $0.telemetry = .minimal
    $0.telemetrySink = MyAnalyticsSink()  // conforms to TelemetrySink protocol
}
```

---

## Cloud Backend

The SDK optionally connects to a cloud control plane. When an API key is provided:

| Feature | What happens |
|---|---|
| **Device registration** | Automatic — hardware profile sent, capability tier assigned |
| **Model manifest** | Backend assigns the right model for each device tier, synced every 6h |
| **OTA updates** | Push new models with canary rollouts and instant kill-switch |
| **Telemetry** | Performance metrics batched and delivered (when enabled) |
| **Device identity** | Stable across reinstalls — same device always gets the same ID |

No cloud calls are made without an API key. Local mode works fully offline.

---

## Models

### Whisper (STT)

| Model | Size | Speed | Best for |
|-------|------|-------|----------|
| `ggml-tiny.en.bin` | 75 MB | 7× real-time | English, mobile-first |
| `ggml-base.bin` | 142 MB | Fast | Multilingual, balanced |
| `ggml-small.bin` | 466 MB | Medium | Higher accuracy |

### LLM (GGUF via llama.cpp)

| Model | Size | Best for |
|-------|------|----------|
| SmolLM2-360M-Instruct (Q4) | ~220 MB | Fastest, mobile-first |
| Qwen2.5-0.5B-Instruct (Q4) | ~400 MB | Multilingual, compact |
| Llama-3.2-1B-Instruct (Q4) | ~700 MB | Strong reasoning |
| SmolLM2-1.7B-Instruct (Q4) | ~1 GB | Balanced |

Browse LLM models with `LlmCatalog`. Download Whisper/TTS models via `ModelRegistry`.

---

## Features

| Feature | Android | iOS |
|---------|---------|-----|
| Speech-to-Text (whisper.cpp) | ✅ | ✅ |
| Text-to-Speech (sherpa-onnx VITS / Kokoro) | ✅ | ✅ |
| System TTS (Apple AVSpeechSynthesizer) | — | ✅ |
| Voice Activity Detection | ✅ | ✅ |
| LLM inference (llama.cpp, GGUF) | ✅ | ✅ |
| Streaming generation | ✅ | ✅ |
| Stateful multi-turn chat | ✅ | ✅ |
| Offline RAG (BM25) | ✅ | ✅ |
| Auto model download (HuggingFace) | ✅ | 🗓 |
| GPU acceleration | ✅ Vulkan | ✅ Metal |
| Cloud backend (registration, manifest, telemetry) | ✅ | ✅ |
| Auto hardware detection | ✅ | ✅ |
| Stable device identity (survives reinstall) | ✅ | ✅ |
| Telemetry (STT/TTS/LLM) | ✅ | ✅ |
| Custom telemetry sink | ✅ | ✅ |
| OTA model rollouts + kill switch | ✅ | ✅ |
| Flutter plugin | 🗓 | 🗓 |
| React Native module | 🗓 | 🗓 |

---

## Platform support

| Platform | STT | TTS | LLM | Version | Status |
|----------|-----|-----|-----|---------|--------|
| Android (API 26+) | ✅ | ✅ | ✅ | 0.3.0-alpha01 | Available |
| iOS 17+ / macOS 14+ | ✅ | ✅ | ✅ | 0.0.1 | Available |
| Flutter | — | — | — | — | Planned |
| React Native | — | — | — | — | Planned |

---

## Benchmarks

| Device | SoC | Model | Audio | Inference | RTF |
|--------|-----|-------|-------|-----------|-----|
| Redmi Note 9 Pro | Snapdragon 720G | whisper-tiny.en | 5.4s | 746ms | **0.14x** |

> RTF < 1.0 = faster than real-time. 0.14x = ~7× faster than real-time.

---

## Building from source

### Android

```bash
git clone https://github.com/deviceai-labs/deviceai.git
cd deviceai
make setup
./gradlew :kotlin:core:compileDebugKotlinAndroid
./gradlew :kotlin:speech:compileDebugKotlinAndroid
./gradlew :kotlin:llm:compileDebugKotlinAndroid
```

### iOS (Swift)

```bash
git clone https://github.com/deviceai-labs/deviceai.git
cd deviceai

# Build XCFrameworks (requires Xcode + CMake)
./sdk/deviceai-commons/scripts/build-xcframeworks.sh

# Build the Swift package
cd swift
swift build
```

---

## Sample App

```bash
# Android: Open samples/androidApp/ in Android Studio and run on device/emulator
# iOS: Open samples/iosApp/ in Xcode
```

---

## Contributing

Issues and PRs welcome. Platform SDK contributions (`flutter/`, `react-native/`) are especially welcome.

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
