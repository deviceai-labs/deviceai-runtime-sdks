# DeviceAI Runtime

**On-device AI runtime for Kotlin, iOS, Flutter, and React Native. Ship speech recognition and synthesis on Android, iOS, and Desktop — no cloud, no latency, no privacy risk.**

[![Build](https://github.com/deviceai-labs/runtime-kmp/actions/workflows/ci.yml/badge.svg)](https://github.com/deviceai-labs/runtime-kmp/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)
[![Maven Central](https://img.shields.io/maven-central/v/dev.deviceai/speech)](https://central.sonatype.com/artifact/dev.deviceai/speech)
[![Kotlin](https://img.shields.io/badge/Kotlin-2.2-blueviolet?logo=kotlin)](https://kotlinlang.org)
[![KMP](https://img.shields.io/badge/Kotlin_Multiplatform-Android%20%7C%20iOS%20%7C%20Desktop-blue)](https://www.jetbrains.com/kotlin-multiplatform/)

---

## What's available

| Module | Who it's for | Distribution | Status |
|--------|-------------|--------------|--------|
| `kmp/core` | Android & Kotlin Multiplatform | Maven Central `dev.deviceai:core` | ✅ Available |
| `kmp/speech` | Android & Kotlin Multiplatform | Maven Central `dev.deviceai:speech` | ✅ Available |
| `kmp/llm` | Android & Kotlin Multiplatform | Maven Central `dev.deviceai:llm` | 🚧 In development |
| `ios/speech` | Swift (iOS only) | Swift Package Index | 🗓 Planned |
| `flutter/speech` | Flutter (Android + iOS) | pub.dev `deviceai_speech` | 🗓 Planned |
| `react-native/speech` | React Native (Android + iOS) | npm `react-native-deviceai-speech` | 🗓 Planned |

**✅ Available** — published and usable today.
**🚧 In development** — module exists; API and native integration not yet complete.
**🗓 Planned** — stub exists to signal intent; no implementation yet.

The Kotlin SDK (`kmp/`) works in both **Kotlin Multiplatform projects and Android-only projects** — it is pure Kotlin with no framework opinion. Lifecycle management, ViewModel wiring, and DI are intentionally left to the app. The SDK exposes `init`, `shutdown`, and `clear` — you integrate them however fits your architecture.

Platform wrappers (`ios/`, `flutter/`, `react-native/`) exist only where there is a language boundary: Swift, Dart, and JavaScript cannot consume Kotlin directly.

---

## Repository structure

```
deviceai/
├── kmp/
│   ├── core/       dev.deviceai:core    ✅  model management, storage, logging
│   ├── speech/     dev.deviceai:speech  ✅  STT (Whisper) + TTS (Piper)
│   └── llm/        dev.deviceai:llm     🚧  LLM inference via llama.cpp
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

## Architecture (kmp/speech)

```
Your App
    │
    ▼
DeviceAIRuntime.configure(Environment.DEVELOPMENT)   ← one-time SDK init
    │
    ├── kmp/core   (dev.deviceai:core)
    │       CoreSDKLogger — structured, environment-aware logging
    │       ModelRegistry — model discovery, download, local management
    │       PlatformStorage — cross-platform file I/O
    │
    └── kmp/speech  (dev.deviceai:speech)
            SpeechBridge — unified STT + TTS Kotlin API
            ModelRegistry — Whisper + Piper model catalog from HuggingFace
                │
                ├── Android / Desktop
                │       JNI → libspeech_jni.so / libspeech_jni.dylib
                │
                └── iOS
                        C Interop → libspeech_merged.a
                            │
                            ├── whisper.cpp  (STT)
                            └── piper + ONNX  (TTS)
```

---

## Features

| Feature | Status |
|---------|--------|
| Speech-to-Text (Whisper) | ✅ Android, iOS, Desktop |
| Text-to-Speech (Piper) | ✅ Android, iOS, Desktop |
| Auto model download (HuggingFace) | ✅ |
| GPU acceleration (Metal / Vulkan) | ✅ |
| Streaming transcription | ✅ |
| Environment-aware logging | ✅ |
| Offline — zero cloud dependency | ✅ |
| LLM inference | 🚧 In development |
| Swift Package | 🗓 Planned |
| Flutter plugin | 🗓 Planned |
| React Native module | 🗓 Planned |

---

## Integration — Kotlin (Android, KMP, Desktop)

Works in any Kotlin project. No KMP setup required for Android-only projects.

### Step 1 — Add dependencies

```kotlin
// build.gradle.kts
implementation("dev.deviceai:core:<version>")
implementation("dev.deviceai:speech:<version>")
```

No extra repository config needed — both artifacts are on Maven Central.

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

---

## Platform support

| Platform | STT | TTS | Sample App |
|----------|-----|-----|------------|
| Android (API 26+) | ✅ | ✅ | ✅ |
| iOS 16+ | ✅ | ✅ | ✅ |
| macOS Desktop | ✅ | ✅ | ✅ |
| Linux | 🚧 | 🚧 | — |
| Windows | 🚧 | 🚧 | — |

---

## Building from source

**Prerequisites:** CMake 3.22+, Android NDK r26+, Xcode 16+ (iOS), Kotlin 2.2+

```bash
git clone --recursive https://github.com/deviceai-labs/runtime-kmp.git
cd runtime-kmp

# Compile checks
./gradlew :kmp:core:compileKotlinJvm
./gradlew :kmp:speech:compileKotlinJvm
./gradlew :kmp:speech:compileDebugKotlinAndroid

# Run the desktop sample app
./gradlew :samples:composeApp:run
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for a deep-dive on the native layer, CMake setup, and module structure.

---

## Roadmap

### `kmp/core` ✅ Available
- [x] `ModelInfo`, `LocalModel`, `PlatformStorage`, `MetadataStore`
- [x] `CoreSDKLogger` — structured, environment-aware logging
- [x] `DeviceAIRuntime` — unified SDK entry point with `Environment` config
- [x] Published: `dev.deviceai:core`

### `kmp/speech` ✅ Available
- [x] STT via whisper.cpp — Android, iOS, Desktop
- [x] TTS via Piper + ONNX — Android, iOS, Desktop
- [x] Model auto-download from HuggingFace
- [x] Published: `dev.deviceai:speech`
- [ ] Streaming TTS
- [ ] Voice activity detection (VAD)

### `kmp/llm` 🚧 In development
- [ ] Local LLM inference via llama.cpp
- [ ] GGUF model support
- [ ] Streaming token generation
- [ ] Will publish: `dev.deviceai:llm`

### `ios/speech` 🗓 Planned
- [ ] Swift `async`/`await` + Combine wrappers around the KMP XCFramework
- [ ] For Swift-only iOS teams that want a pure Swift dependency
- [ ] Will distribute via Swift Package Index

### `flutter/speech` 🗓 Planned
- [ ] Flutter plugin bridging the Kotlin SDK (Android) and Swift Package (iOS)
- [ ] Will publish: pub.dev `deviceai_speech`

### `react-native/speech` 🗓 Planned
- [ ] TurboModule bridging the Kotlin SDK (Android) and Swift Package (iOS)
- [ ] Will publish: npm `react-native-deviceai-speech`

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Issues and PRs welcome.

Platform wrapper contributions (`ios/`, `flutter/`, `react-native/`) are especially
welcome — each stub directory contains a README with the expected API surface.

---

## Sample App

`samples/composeApp/` is a working Compose Multiplatform demo — auto-downloads whisper-tiny on first launch, records audio, and shows transcription with live latency. Runs on Android, iOS, and Desktop.

```bash
# Desktop
./gradlew :samples:composeApp:run

# Android — open in Android Studio and run on device/emulator

# iOS — open samples/iosApp/iosApp.xcodeproj in Xcode and run
```
