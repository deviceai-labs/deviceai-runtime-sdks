# DeviceAI Runtime

**On-device AI runtime for Android, Desktop, iOS, Flutter, and React Native. Ship speech recognition, synthesis, and LLM inference — no cloud required, no latency, no privacy risk.**

[![Build](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml/badge.svg)](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)
[![Maven Central](https://img.shields.io/maven-central/v/dev.deviceai/speech)](https://central.sonatype.com/artifact/dev.deviceai/speech)
[![Kotlin](https://img.shields.io/badge/Kotlin-2.2-blueviolet?logo=kotlin)](https://kotlinlang.org)
[![Android](https://img.shields.io/badge/Platform-Android-green)](https://developer.android.com)

---

## What's available

| Module | Language | Distribution | Status |
|--------|----------|--------------|--------|
| `kotlin/core` | Kotlin (Android + JVM) | Maven Central `dev.deviceai:core` | ✅ Available |
| `kotlin/speech` | Kotlin (Android + JVM) | Maven Central `dev.deviceai:speech` | ✅ Available |
| `kotlin/llm` | Kotlin (Android + JVM) | Maven Central `dev.deviceai:llm` | ✅ Available |
| `swift/` | Swift | Swift Package Manager | 🚧 In progress |
| `flutter/speech` | Dart | pub.dev `deviceai_speech` | 🗓 Planned |
| `react-native/speech` | TypeScript | npm `react-native-deviceai-speech` | 🗓 Planned |

Each SDK is **independent and native to its platform** — they all call the same C++ engines (whisper.cpp, sherpa-onnx, llama.cpp) directly, with no cross-language bridging. The Kotlin SDK targets Android and Desktop JVM. iOS is served by a dedicated Swift SDK.

---

## Repository structure

```
deviceai/
├── kotlin/
│   ├── core/       dev.deviceai:core    ✅  model management, storage, logging
│   ├── speech/     dev.deviceai:speech  ✅  STT (Whisper) + TTS (sherpa-onnx) + VAD
│   └── llm/        dev.deviceai:llm     ✅  LLM inference via llama.cpp + offline RAG
├── swift/              Swift Package            🚧  Native iOS/macOS SDK
├── flutter/
│   └── speech/     pub.dev: deviceai_speech 🗓  Flutter plugin
├── react-native/
│   └── speech/     npm: react-native-deviceai-speech  🗓  TurboModule
└── samples/
    ├── androidApp/ Android demo app            ✅
    └── iosApp/     native iOS sample           🚧 In progress
```

---

## Integration — Android (Kotlin)

### Step 1 — Add dependencies

```kotlin
// build.gradle.kts
implementation("dev.deviceai:core:0.3.0-alpha01")
implementation("dev.deviceai:speech:0.3.0-alpha01")   // STT + TTS + VAD
implementation("dev.deviceai:llm:0.3.0-alpha01")      // LLM inference + RAG
```

No extra repository config needed — all artifacts are on Maven Central.

---

### Step 2 — Initialize the SDK

Call `DeviceAI.initialize()` **once** at app startup before using any module.

#### Android

```kotlin
import dev.deviceai.core.DeviceAI
import dev.deviceai.core.Environment
import dev.deviceai.models.PlatformStorage

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        PlatformStorage.initialize(this)
        DeviceAI.initialize(context = this) {
            environment = Environment.Development
        }
        setContent { App() }
    }
}
```

#### With cloud backend (Staging / Production)

```kotlin
DeviceAI.initialize(context = this, apiKey = "dai_live_...") {
    environment   = Environment.Production
    telemetry     = Telemetry.Enabled
    appVersion    = BuildConfig.VERSION_NAME
    appAttributes = mapOf("user_tier" to "premium")
}
```

---

### Step 3 — Download a model

`ModelRegistry` fetches the catalog from HuggingFace and downloads models to local storage. Downloads are resumable on interruption.

```kotlin
import dev.deviceai.models.ModelRegistry

val model = ModelRegistry.getOrDownload("ggml-tiny.en.bin") { progress ->
    println("${progress.percentComplete.toInt()}% — ${progress.bytesDownloaded / 1_000_000}MB")
}
```

> **whisper-tiny.en** (75 MB) runs 7× faster than real-time on mid-range Android hardware.

---

### Step 4 — Transcribe speech

```kotlin
import dev.deviceai.SpeechBridge
import dev.deviceai.SttConfig

SpeechBridge.initStt(model.modelPath, SttConfig(language = "en", useGpu = true))

val text: String = SpeechBridge.transcribeAudio(samples) // FloatArray, 16kHz mono PCM
// or
val text: String = SpeechBridge.transcribe("/path/to/audio.wav")

SpeechBridge.shutdownStt()
```

---

### Step 5 — Synthesize speech (optional)

```kotlin
import dev.deviceai.SpeechBridge
import dev.deviceai.TtsConfig

SpeechBridge.initTts(
    modelPath  = voice.modelPath,
    tokensPath = voice.tokensPath,
    config     = TtsConfig(speechRate = 1.0f)
)

val pcm: ShortArray = SpeechBridge.synthesize("Hello from DeviceAI.")
// Play with AudioTrack

SpeechBridge.shutdownTts()
```

---

### Step 6 — Run a local LLM

```kotlin
import dev.deviceai.core.DeviceAI
import dev.deviceai.llm.llm

// Create a chat session — model loads once, history is automatic
val session = DeviceAI.llm.chat("/path/to/model.gguf") {
    systemPrompt = "You are a helpful assistant."
    maxTokens    = 512
    temperature  = 0.7f
    useGpu       = true
}

// Streaming (recommended for UI)
session.send("What is Kotlin Multiplatform?")
    .collect { token -> print(token) }

// Multi-turn — history managed automatically
session.send("Give me a code example.").collect { print(it) }

// Blocking (scripts / tests)
val reply = session.sendBlocking("Summarise in one line.")

// Lifecycle
session.cancel()       // abort in-progress generation
session.clearHistory() // fresh conversation, model stays loaded
session.close()        // unload model, free resources
```

---

### Step 7 — Offline RAG (optional)

Attach a `BM25RagStore` to inject local documents as context — no embedding model required.

```kotlin
import dev.deviceai.llm.rag.BM25RagStore

val store = BM25RagStore(rawChunks = listOf(
    "DeviceAI supports Android, iOS, and Desktop.",
    "LLM inference uses llama.cpp with Metal on Apple Silicon."
))

val session = DeviceAI.llm.chat("/path/to/model.gguf") {
    ragStore = store
}

session.send("Which platforms does DeviceAI support?").collect { print(it) }
```

---

## Environments

| Environment | API key | Backend | Log level | Use for |
|-------------|---------|---------|-----------|---------|
| `Development` | not required | none — local model path | DEBUG | local dev, unit tests |
| `Staging` | required | staging.api.deviceai.dev | DEBUG | pre-release QA |
| `Production` | required | api.deviceai.dev | WARN | release builds |

---

## Architecture

```
Your App
    │
    ▼
DeviceAI.initialize(context, apiKey) { environment = Environment.Development }
    │
    ├── kotlin/core   (dev.deviceai:core)
    │       DeviceAI           — unified SDK entry point
    │       CoreSDKLogger       — structured, environment-aware logging
    │       ModelRegistry       — model discovery, download, local management
    │       PlatformStorage     — Android file I/O
    │
    ├── kotlin/speech  (dev.deviceai:speech)
    │       SpeechBridge        — unified STT + TTS Kotlin API
    │           │
    │           └── JNI → libspeech_jni.so
    │                   ├── whisper.cpp   (STT)
    │                   └── sherpa-onnx   (TTS + VAD)
    │
    └── kotlin/llm  (dev.deviceai:llm)
            DeviceAI.llm.chat()   — creates a ChatSession
            ChatSession            — stateful conversation, streaming Flow<String>
            BM25RagStore           — offline retrieval-augmented generation
                │
                └── JNI → libdeviceai_llm_jni.so
                        └── llama.cpp (Vulkan GPU)
```

---

## Features

| Feature | Status |
|---------|--------|
| Speech-to-Text (Whisper) | ✅ Android |
| Text-to-Speech (sherpa-onnx VITS / Kokoro) | ✅ Android |
| Voice Activity Detection (Silero VAD) | ✅ Android |
| LLM inference (llama.cpp) | ✅ Android |
| Offline RAG (BM25) | ✅ Android |
| Streaming LLM generation (`Flow<String>`) | ✅ Android |
| Stateful `ChatSession` with auto history | ✅ |
| Auto model download (HuggingFace) | ✅ |
| GPU acceleration (Metal / Vulkan) | ✅ |
| Cloud backend — OTA models, telemetry | 🚧 In progress |
| Swift SDK (iOS / macOS) | 🚧 In progress |
| Flutter plugin | 🗓 Planned |
| React Native module | 🗓 Planned |
| Tool calling / voice agents | 🗓 Planned |

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
| SmolLM2-1.7B-Instruct (Q4) | ~1 GB | Balanced |
| Qwen2.5-0.5B-Instruct (Q4) | ~400 MB | Multilingual, compact |
| Llama-3.2-1B-Instruct (Q4) | ~700 MB | Strong reasoning |

Browse all available models via `LlmCatalog`.

---

## Platform support

### Kotlin SDK (this repo)

| Platform | STT | TTS | LLM | Sample App |
|----------|-----|-----|-----|------------|
| Android (API 26+) | ✅ | ✅ | ✅ | ✅ |

### Coming soon

| Platform | SDK | Status |
|----------|-----|--------|
| iOS | Swift Package (`swift/`) | 🚧 In progress |
| iOS sample app | Native SwiftUI (`samples/iosApp/`) | 🚧 In progress |
| macOS Desktop | Swift Package (`swift/`) | 🚧 In progress |
| Flutter | Dart plugin | 🗓 Planned |
| React Native | TurboModule | 🗓 Planned |

---

## Benchmarks

| Device | Chip | Model | Audio | Inference | RTF |
|--------|------|-------|-------|-----------|-----|
| Redmi Note 9 Pro | Snapdragon 720G | whisper-tiny | 5.4s | 746ms | **0.14x** |

> RTF < 1.0 = faster than real-time. 0.14x = ~7× faster than real-time on a mid-range Android phone.

---

## Building from source

**Prerequisites:** CMake 3.22+, Android NDK r26+, Kotlin 2.2+, Android Studio

```bash
git clone --recursive https://github.com/deviceai-labs/deviceai.git
cd deviceai

# Compile checks
./gradlew :kotlin:core:compileDebugKotlinAndroid
./gradlew :kotlin:speech:compileDebugKotlinAndroid
./gradlew :kotlin:llm:compileDebugKotlinAndroid
```

---

## Roadmap

- [x] Kotlin SDK — speech, LLM, RAG, streaming
- [x] `DeviceAI` unified entry point with `Environment` + `CloudConfig` DSL
- [x] `ChatSession` — stateful multi-turn LLM conversations
- [ ] Backend integration — device registration, OTA model assignment, telemetry
- [ ] Swift SDK — native iOS/macOS package (in progress)
- [ ] Flutter SDK
- [ ] React Native SDK
- [ ] Tool calling / voice agents (`DeviceAI.agent`)

---

## Sample App

`samples/androidApp/` is a working Android demo app.

```bash
# Open in Android Studio and run on device/emulator
```

---

## Contributing

Issues and PRs welcome. Platform wrapper contributions (`ios/`, `flutter/`, `react-native/`) are especially welcome — each stub directory contains a README with the expected API surface.
