# DeviceAI Runtime

**On-device AI runtime for Kotlin, iOS, Flutter, and React Native. Ship speech recognition, synthesis, and LLM inference on Android, iOS, and Desktop — no cloud, no latency, no privacy risk.**

[![Build](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml/badge.svg)](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)
[![Maven Central](https://img.shields.io/maven-central/v/dev.deviceai/speech)](https://central.sonatype.com/artifact/dev.deviceai/speech)
[![Kotlin](https://img.shields.io/badge/Kotlin-2.2-blueviolet?logo=kotlin)](https://kotlinlang.org)
[![KMP](https://img.shields.io/badge/Kotlin_Multiplatform-Android%20%7C%20iOS%20%7C%20Desktop-blue)](https://www.jetbrains.com/kotlin-multiplatform/)

---

## What's available

| Module | Language | Distribution | Status |
|--------|----------|--------------|--------|
| `kotlin/core` | Kotlin (Android + KMP) | Maven Central `dev.deviceai:core` | ✅ Available |
| `kotlin/speech` | Kotlin (Android + KMP) | Maven Central `dev.deviceai:speech` | ✅ Available |
| `kotlin/llm` | Kotlin (Android + KMP) | Maven Central `dev.deviceai:llm` | ✅ Available |
| `ios/speech` | Swift | Swift Package Index | 🗓 Planned |
| `flutter/speech` | Dart | pub.dev `deviceai_speech` | 🗓 Planned |
| `react-native/speech` | TypeScript | npm `react-native-deviceai-speech` | 🗓 Planned |

**✅ Available** — published and usable today.
**🗓 Planned** — stub exists to signal intent; no implementation yet.

Each SDK is **independent and native to its platform** — they all call the same C++ engines (whisper.cpp, piper, llama.cpp) directly, with no cross-language bridging:

- `kotlin/` — Kotlin API, JNI bridge to C++ on Android/JVM, C interop on iOS (for KMP projects)
- `ios/` — Swift API, links C++ engines directly as a Swift Package binary target
- `flutter/` — Dart API, calls C++ via `dart:ffi` on Android and iOS
- `react-native/` — TypeScript API, calls C++ via JSI (New Architecture) on Android and iOS

---

## Repository structure

```
deviceai/
├── kotlin/
│   ├── core/       dev.deviceai:core    ✅  model management, storage, logging
│   ├── speech/     dev.deviceai:speech  ✅  STT (Whisper) + TTS (Piper)
│   └── llm/        dev.deviceai:llm     ✅  LLM inference via llama.cpp + offline RAG
├── ios/
│   └── speech/     Swift Package            🗓  Swift async/await wrapper
├── flutter/
│   └── speech/     pub.dev: deviceai_speech 🗓  Flutter plugin
├── react-native/
│   └── speech/     npm: react-native-deviceai-speech  🗓  TurboModule
└── samples/
    ├── composeApp/ Compose Multiplatform demo  ✅
    └── iosApp/     native iOS shell            ✅
```

---

## Why DeviceAI Runtime?

Mobile AI is broken for most teams:

- **Fragmented SDKs** — separate wrappers for Android, iOS, and desktop that never stay in sync
- **Cloud dependency** — latency, cost, and user data leaving the device
- **Model loading is messy** — threading, memory pressure, cold-start, and caching reinvented every time
- **Every team writes the same wrapper** — from scratch, badly

DeviceAI Runtime gives you a single API: one integration, all platforms, fully local.

---

## Benchmarks

Real numbers on real hardware.

| Device | Chip | Model | Audio | Inference | RTF |
|--------|------|-------|-------|-----------|-----|
| Redmi Note 9 Pro | Snapdragon 720G | whisper-tiny | 5.4s | 746ms | **0.14x** |

> RTF < 1.0 = faster than real-time. 0.14x = ~7× faster than real-time on a mid-range Android phone.

---

## Architecture

```
Your App
    │
    ▼
DeviceAIRuntime.configure(Environment.DEVELOPMENT)   ← one-time SDK init
    │
    ├── kotlin/core   (dev.deviceai:core)
    │       CoreSDKLogger — structured, environment-aware logging
    │       ModelRegistry — model discovery, download, local management
    │       PlatformStorage — cross-platform file I/O
    │
    ├── kotlin/speech  (dev.deviceai:speech)
    │       SpeechBridge — unified STT + TTS Kotlin API
    │       ModelRegistry — Whisper + Piper model catalog from HuggingFace
    │           │
    │           ├── Android / Desktop  →  JNI → libspeech_jni.so/.dylib
    │           └── iOS  →  C Interop → libspeech_merged.a
    │                           ├── whisper.cpp  (STT)
    │                           └── piper + ONNX  (TTS)
    │
    └── kotlin/llm  (dev.deviceai:llm)
            LlmBridge — chat API with streaming Flow<String>
            BM25RagStore — offline retrieval-augmented generation
                │
                ├── Android / Desktop  →  JNI → libdeviceai_llm_jni.so/.dylib
                └── iOS  →  C Interop → libllm_merged.a
                                └── llama.cpp (Metal + CoreML)
```

---

## Features

| Feature | Status |
|---------|--------|
| Speech-to-Text (Whisper) | ✅ Android, iOS, Desktop |
| Text-to-Speech (Piper) | ✅ Android, iOS, Desktop |
| LLM inference (llama.cpp) | ✅ Android, iOS, Desktop |
| Offline RAG (BM25) | ✅ Android, iOS, Desktop |
| Streaming LLM generation (Flow) | ✅ Android, iOS, Desktop |
| Auto model download (HuggingFace) | ✅ |
| GPU acceleration (Metal / Vulkan) | ✅ |
| Environment-aware logging | ✅ |
| Offline — zero cloud dependency | ✅ |
| Swift Package | 🗓 Planned |
| Flutter plugin | 🗓 Planned |
| React Native module | 🗓 Planned |

---

## Integration — Kotlin (Android, KMP, Desktop)

Works in any Kotlin project. No KMP setup required for Android-only projects.

### Step 1 — Add dependencies

```kotlin
// build.gradle.kts
implementation("dev.deviceai:core:0.2.0-alpha01")
implementation("dev.deviceai:speech:0.2.0-alpha01")   // STT + TTS
implementation("dev.deviceai:llm:0.2.0-alpha01")      // LLM inference + RAG
```

No extra repository config needed — all artifacts are on Maven Central.

---

### Step 2 — Initialize the SDK

Call `DeviceAIRuntime.configure()` **once**, before any other SDK call.

#### Android

```kotlin
import dev.deviceai.core.DeviceAIRuntime
import dev.deviceai.core.Environment
import dev.deviceai.models.PlatformStorage

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        DeviceAIRuntime.configure(Environment.DEVELOPMENT)
        PlatformStorage.initialize(this) // Android needs a Context for file storage
        setContent { App() }
    }
}
```

#### iOS (Kotlin side of KMP project)

```kotlin
import dev.deviceai.core.DeviceAIRuntime
import dev.deviceai.core.Environment

fun MainViewController(): UIViewController {
    DeviceAIRuntime.configure(Environment.DEVELOPMENT)
    return ComposeUIViewController { App() }
}
```

> **Info.plist** — add the microphone usage description:
> ```xml
> <key>NSMicrophoneUsageDescription</key>
> <string>Used for on-device speech recognition.</string>
> <key>CADisableMinimumFrameDurationOnPhone</key>
> <true/>
> ```

#### Desktop

```kotlin
import dev.deviceai.core.DeviceAIRuntime
import dev.deviceai.core.Environment

fun main() = application {
    DeviceAIRuntime.configure(Environment.DEVELOPMENT)
    Window(onCloseRequest = ::exitApplication, title = "My App") { App() }
}
```

---

### Step 3 — Download a model

`ModelRegistry` fetches the catalog from HuggingFace and downloads models to local storage. Downloads resume automatically on interruption.

```kotlin
import dev.deviceai.models.ModelRegistry

val model = ModelRegistry.getOrDownload("ggml-tiny.en.bin") { progress ->
    println("${progress.percentComplete.toInt()}% — ${progress.bytesDownloaded / 1_000_000}MB")
}
```

> **whisper-tiny.en** (75MB) is the recommended starting point — it runs 7× faster than real-time on mid-range Android hardware.

---

### Step 4 — Transcribe speech

```kotlin
import dev.deviceai.SpeechBridge
import dev.deviceai.SttConfig

SpeechBridge.initStt(model.modelPath, SttConfig(language = "en", useGpu = true))

val text: String = SpeechBridge.transcribeAudio(samples) // FloatArray of 16kHz mono PCM
// or
val text: String = SpeechBridge.transcribe("/path/to/audio.wav")

SpeechBridge.shutdownStt() // call from onCleared(), onDestroy(), or equivalent
```

---

### Step 5 — Synthesize speech (optional)

```kotlin
import dev.deviceai.SpeechBridge
import dev.deviceai.TtsConfig

SpeechBridge.initTts(
    modelPath  = voice.modelPath,
    configPath = voice.configPath!!,
    config     = TtsConfig(speechRate = 1.0f)
)

val pcm: ShortArray = SpeechBridge.synthesize("Hello from DeviceAI.")
// Play pcm with AudioTrack (Android), AVAudioEngine (iOS), or javax.sound (Desktop)

SpeechBridge.shutdownTts()
```

---

### Step 6 — Run a local LLM

```kotlin
import dev.deviceai.llm.LlmBridge
import dev.deviceai.llm.LlmInitConfig
import dev.deviceai.llm.LlmGenConfig
import dev.deviceai.llm.LlmMessage
import dev.deviceai.llm.LlmRole

LlmBridge.initLlm(
    modelPath = llmModel.modelPath,
    config    = LlmInitConfig(contextSize = 2048, maxThreads = 4, useGpu = true)
)

val messages = listOf(
    LlmMessage(LlmRole.SYSTEM, "You are a helpful assistant."),
    LlmMessage(LlmRole.USER, "What is Kotlin Multiplatform?")
)

// Streaming (recommended)
LlmBridge.generateStream(messages, LlmGenConfig(maxTokens = 512))
    .collect { token -> print(token) }

// Blocking
val result = LlmBridge.generate(messages, LlmGenConfig(maxTokens = 512))
println(result.text)

LlmBridge.shutdown()
```

---

### Step 7 — Offline RAG (optional)

Attach a `BM25RagStore` to inject local documents as context — no embedding model required.

```kotlin
import dev.deviceai.llm.rag.BM25RagStore
import dev.deviceai.llm.rag.RagChunk

val store = BM25RagStore()
store.addChunks(listOf(
    RagChunk("1", "DeviceAI supports Android, iOS, and Desktop."),
    RagChunk("2", "LLM inference uses llama.cpp with Metal on Apple Silicon.")
))

LlmBridge.generateStream(
    messages = messages,
    config   = LlmGenConfig(maxTokens = 512, ragStore = store)
).collect { print(it) }
```

---

## Logging

`DeviceAIRuntime.configure()` sets the log verbosity automatically:

| Environment | Min level | What you see |
|-------------|-----------|--------------|
| `DEVELOPMENT` | `DEBUG` | Everything — debug, info, warnings, errors |
| `PRODUCTION` | `WARN` | Warnings and errors only |

Forward SDK logs to your own backend (Crashlytics, Datadog, Sentry, etc.):

```kotlin
DeviceAIRuntime.configure(
    environment = Environment.PRODUCTION,
    logHandler  = { event ->
        Crashlytics.log("${event.level} [${event.tag}] ${event.message}")
        event.throwable?.let { Crashlytics.recordException(it) }
    }
)
```

---

## Models

### Whisper (STT) — via [ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp)

| Model | Size | Speed | Best for |
|-------|------|-------|----------|
| `ggml-tiny.en.bin` | 75 MB | 7× real-time | English, mobile-first |
| `ggml-base.bin` | 142 MB | Fast | Multilingual, balanced |
| `ggml-small.bin` | 466 MB | Medium | Higher accuracy |
| `ggml-medium.bin` | 1.5 GB | Slow | Desktop / server |

### Piper (TTS) — via [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices)

| Voice | Size | Language |
|-------|------|----------|
| `en_US-lessac-medium` | 60 MB | English (US) |
| `en_GB-alba-medium` | 55 MB | English (UK) |
| `de_DE-thorsten-medium` | 65 MB | German |

Browse all voices via `ModelRegistry.getPiperVoices()` — filters by language and quality.

### LLM — via llama.cpp (GGUF format)

| Model | Size | Best for |
|-------|------|----------|
| SmolLM2-360M-Instruct | ~220 MB | Fastest, mobile-first |
| SmolLM2-1.7B-Instruct | ~1 GB | Balanced |
| Qwen2.5-0.5B-Instruct | ~400 MB | Multilingual, compact |
| Qwen2.5-1.5B-Instruct | ~900 MB | Multilingual, quality |

Browse available models via `LlmCatalog`.

---

## Platform support

| Platform | STT | TTS | LLM | Sample App |
|----------|-----|-----|-----|------------|
| Android (API 26+) | ✅ | ✅ | ✅ | ✅ |
| iOS 17+ | ✅ | ✅ | ✅ | ✅ |
| macOS Desktop | ✅ | ✅ | ✅ | ✅ |
| Linux | 🚧 | 🚧 | 🚧 | — |
| Windows | 🚧 | 🚧 | 🚧 | — |

---

## Building from source

**Prerequisites:** CMake 3.22+, Android NDK r26+, Xcode 26+ (iOS), Kotlin 2.2+

```bash
git clone --recursive https://github.com/deviceai-labs/deviceai.git
cd deviceai

# Compile checks
./gradlew :kotlin:core:compileKotlinJvm
./gradlew :kotlin:speech:compileKotlinJvm
./gradlew :kotlin:speech:compileDebugKotlinAndroid
./gradlew :kotlin:llm:compileKotlinJvm
./gradlew :kotlin:llm:compileDebugKotlinAndroid

# Run the desktop sample app
./gradlew :samples:composeApp:run
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for a deep-dive on the native layer, CMake setup, and module structure.

---

## Roadmap

### `kotlin/core` ✅ Available
- [x] `ModelInfo`, `LocalModel`, `PlatformStorage`, `MetadataStore`
- [x] `CoreSDKLogger` — structured, environment-aware logging
- [x] `DeviceAIRuntime` — unified SDK entry point with `Environment` config
- [x] Published: `dev.deviceai:core`

### `kotlin/speech` ✅ Available
- [x] STT via whisper.cpp — Android, iOS, Desktop
- [x] TTS via Piper + ONNX — Android, iOS, Desktop
- [x] Model auto-download from HuggingFace
- [x] Voice activity detection (VAD) — adaptive energy-based, trims silence pre-inference
- [x] Published: `dev.deviceai:speech`
- [ ] Streaming TTS

### `kotlin/llm` ✅ Available
- [x] LLM inference via llama.cpp — Android, iOS, Desktop
- [x] GGUF model support (SmolLM2, Qwen2.5, and any GGUF-compatible model)
- [x] Streaming token generation via `Flow<String>`
- [x] Multi-turn conversation with `List<LlmMessage>`
- [x] Offline RAG via `BM25RagStore` — no embedding model required
- [x] GPU acceleration (Metal on iOS/macOS, Vulkan on Android)
- [x] Published: `dev.deviceai:llm`

### `ios/speech` 🗓 Planned
- [ ] Native Swift SDK — links whisper.cpp + piper directly, no KMP dependency
- [ ] `SpeechRecognizer` and `SpeechSynthesizer` with `async`/`await` + Combine
- [ ] Will distribute via Swift Package Index

### `flutter/speech` 🗓 Planned
- [ ] Native Dart SDK — calls C++ engines via `dart:ffi` on Android and iOS
- [ ] `DeviceAISpeech` Dart class with stream-based transcription
- [ ] Will publish: pub.dev `deviceai_speech`

### `react-native/speech` 🗓 Planned
- [ ] Native TypeScript SDK — calls C++ engines via JSI (New Architecture) on Android and iOS
- [ ] Full TypeScript types, event-based callbacks
- [ ] Will publish: npm `react-native-deviceai-speech`

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Issues and PRs welcome.

Platform wrapper contributions (`ios/`, `flutter/`, `react-native/`) are especially
welcome — each stub directory contains a README with the expected API surface.

---

## Sample App

`samples/composeApp/` is a working Compose Multiplatform demo — auto-downloads models on first launch, records audio, transcribes speech, and runs local LLM chat. Runs on Android, iOS, and Desktop.

```bash
# Desktop
./gradlew :samples:composeApp:run

# Android — open in Android Studio and run on device/emulator

# iOS — open samples/iosApp/iosApp.xcodeproj in Xcode and run
```
