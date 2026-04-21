# DeviceAI

**On-device AI inference for Android — speech recognition, text-to-speech, and LLM generation. Optionally managed from the cloud with OTA model updates, telemetry, and cohort targeting.**

[![Build](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml/badge.svg)](https://github.com/deviceai-labs/deviceai/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)
[![Maven Central](https://img.shields.io/maven-central/v/dev.deviceai/core)](https://central.sonatype.com/artifact/dev.deviceai/core)
[![Kotlin](https://img.shields.io/badge/Kotlin-2.2-blueviolet?logo=kotlin)](https://kotlinlang.org)
[![Android](https://img.shields.io/badge/Platform-Android-green)](https://developer.android.com)

---

## What DeviceAI does

1. **On-device inference** — STT (whisper.cpp), TTS (sherpa-onnx), LLM (llama.cpp) run entirely on the device. No cloud latency, no privacy risk.
2. **Cloud control plane** (optional) — register devices, push OTA model updates, collect telemetry, target cohorts by device capability, kill-switch bad rollouts instantly.
3. **Auto hardware detection** — RAM, CPU cores, SoC model, NNAPI availability detected automatically. Backend assigns the right model for each device.

---

## SDKs

| Module | Language | Distribution | Status |
|--------|----------|--------------|--------|
| `kotlin/core` | Kotlin (Android) | Maven Central `dev.deviceai:core` | ✅ Available |
| `kotlin/speech` | Kotlin (Android) | Maven Central `dev.deviceai:speech` | ✅ Available |
| `kotlin/llm` | Kotlin (Android) | Maven Central `dev.deviceai:llm` | ✅ Available |
| `swift/` | Swift (iOS / macOS) | Swift Package Manager | 🚧 In progress |
| `flutter/` | Dart | pub.dev | 🗓 Planned |
| `react-native/` | TypeScript | npm | 🗓 Planned |

Each SDK calls the same C++ engines directly via `sdk/deviceai-commons/` — no cross-language wrapping.

---

## Repository structure

```
deviceai/
├── sdk/
│   └── deviceai-commons/        Pure C++ shared layer (all platforms)
│       ├── VERSIONS             Pinned deps (whisper, llama, sherpa-onnx)
│       ├── CMakeLists.txt       Root CMake with feature flags
│       ├── cmake/               FetchContent for C++ dependencies
│       ├── scripts/             Pre-built binary download scripts
│       └── src/
│           ├── core/            Session, telemetry, backend client (C headers + impl)
│           ├── backends/        whisper/, llamacpp/, sherpa_onnx/
│           ├── bridges/jni/     JNI bridge C++ (Android)
│           └── utils/           json_builder
│
├── kotlin/
│   ├── core/                    dev.deviceai:core
│   ├── speech/                  dev.deviceai:speech
│   └── llm/                    dev.deviceai:llm
│
├── swift/                       Swift SDK (in progress)
├── samples/androidApp/          Android demo app
└── .github/workflows/           CI + publish
```

---

## Quick start — Android

### 1. Add dependencies

```kotlin
// build.gradle.kts
implementation("dev.deviceai:core:0.3.0-alpha01")
implementation("dev.deviceai:speech:0.3.0-alpha01")
implementation("dev.deviceai:llm:0.3.0-alpha01")
```

### 2. Initialize

```kotlin
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        PlatformStorage.initialize(this)

        // Local mode — no cloud, no API key
        DeviceAI.initialize(context = this) {
            environment = Environment.Development
        }

        setContent { App() }
    }
}
```

### With cloud backend

```kotlin
DeviceAI.initialize(context = this, apiKey = "dai_live_...") {
    environment = Environment.Staging
    telemetry   = TelemetryLevel.Minimal
    appVersion  = BuildConfig.VERSION_NAME
    // Hardware (RAM, CPU, SoC, NNAPI) is auto-detected — no need to set it.
    // Add custom targeting attributes only:
    appAttributes = mapOf("user_tier" to "premium")
}
```

The SDK auto-detects device capabilities and sends them at registration:
```json
{
  "capability_profile": {
    "ram_gb": 7.6, "cpu_cores": 8, "has_nnapi": true,
    "soc_model": "SM8550", "storage_available_mb": 45000,
    "sdk_version": "0.3.0-alpha01", "platform": "android"
  }
}
```

The backend scores the device tier and assigns the right model.

### 3. Speech-to-Text

```kotlin
SpeechBridge.initStt(modelPath, SttConfig(language = "en", useGpu = true))
val text = SpeechBridge.transcribeAudio(samples) // FloatArray, 16kHz mono
SpeechBridge.shutdownStt()
```

### 4. Text-to-Speech

```kotlin
SpeechBridge.initTts(modelPath, tokensPath, TtsConfig(speechRate = 1.0f))
val pcm: ShortArray = SpeechBridge.synthesize("Hello from DeviceAI.")
SpeechBridge.shutdownTts()
```

### 5. LLM chat

```kotlin
val session = DeviceAI.llm.chat("/path/to/model.gguf") {
    systemPrompt = "You are a helpful assistant."
    maxTokens = 512; temperature = 0.7f; useGpu = true
}

// Streaming
session.send("What is Kotlin?").collect { token -> print(token) }

// Multi-turn — history automatic
session.send("Give me an example.").collect { print(it) }

// Lifecycle
session.cancel()        // abort generation
session.clearHistory()  // fresh conversation
session.close()         // unload model
```

### 6. Offline RAG

```kotlin
val store = BM25RagStore(rawChunks = listOf(
    "DeviceAI supports Android and iOS.",
    "LLM inference uses llama.cpp with Vulkan GPU."
))
val session = DeviceAI.llm.chat("/path/to/model.gguf") { ragStore = store }
session.send("What GPU does DeviceAI use?").collect { print(it) }
```

---

## Cloud control plane

The SDK optionally connects to a Go backend for managed mode:

| Feature | What it does |
|---|---|
| **Device registration** | Auto-register with capability profile, get JWT token (30-day, auto-refresh) |
| **Manifest sync** | Backend assigns models per device tier (low/mid/high/flagship), synced every 6h |
| **OTA model updates** | Push new models via canary → rollout → full, with instant kill-switch |
| **Telemetry** | Inference latency, TTFT, tokens/sec, model load time — batched, network-aware |
| **Cohort targeting** | Deterministic bucketing by device capabilities, app version, custom attributes |

### Environments

| Environment | Base URL | API key |
|---|---|---|
| `Development` | `localhost:8080` | Not required |
| `Staging` | `staging.api.deviceai.dev` | Required |
| `Production` | `api.deviceai.dev` | Required |

Backend repo: [`deviceai-labs/cloud`](https://github.com/deviceai-labs/cloud)

---

## Architecture

```
Your App
    │
    ▼
DeviceAI.initialize(context, apiKey)
    │
    ├── kotlin/core (dev.deviceai:core)
    │       DeviceAI             entry point + cloud bootstrap
    │       DeviceCapabilities   auto-detect RAM, CPU, SoC, NNAPI
    │       TelemetryEngine      three-priority buffer (normal/wifi/critical)
    │       BackendClient        device registration, manifest, telemetry
    │       CoreJniBridge    ──→ JNI → libdeviceai_core_jni.so
    │                                  └── sdk/deviceai-commons/src/core/
    │
    ├── kotlin/speech (dev.deviceai:speech)
    │       SpeechBridge     ──→ JNI → libspeech_jni.so
    │                                  ├── whisper.cpp (STT)
    │                                  └── sherpa-onnx (TTS + VAD)
    │
    └── kotlin/llm (dev.deviceai:llm)
            ChatSession
            BM25RagStore     ──→ JNI → libdeviceai_llm_jni.so
                                       └── llama.cpp (Vulkan GPU)
```

C++ dependencies fetched at build time via CMake FetchContent (no git submodules).

---

## Features

| Feature | Status |
|---------|--------|
| Speech-to-Text (whisper.cpp) | ✅ Android |
| Text-to-Speech (sherpa-onnx VITS / Kokoro) | ✅ Android |
| Voice Activity Detection | ✅ Android |
| LLM inference (llama.cpp, GGUF) | ✅ Android |
| Streaming generation (`Flow<String>`) | ✅ Android |
| Stateful `ChatSession` with auto history | ✅ |
| Offline RAG (BM25) | ✅ |
| Auto model download (HuggingFace) | ✅ |
| GPU acceleration (Vulkan) | ✅ |
| Auto hardware detection | ✅ |
| Cloud backend — registration, manifest, telemetry | ✅ Staging live |
| SoC-based capability tier scoring | ✅ Server-side |
| OTA model rollouts + kill switch | ✅ Backend ready |
| Swift SDK (iOS / macOS) | 🚧 In progress |
| Flutter plugin | 🗓 Planned |
| React Native module | 🗓 Planned |

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

Browse all available models via `LlmCatalog`.

---

## Platform support

| Platform | STT | TTS | LLM | Cloud | Status |
|----------|-----|-----|-----|-------|--------|
| Android (API 26+) | ✅ | ✅ | ✅ | ✅ | Available |
| iOS / macOS | — | — | — | — | Swift SDK in progress |
| Flutter | — | — | — | — | Planned |
| React Native | — | — | — | — | Planned |

---

## Building from source

```bash
git clone https://github.com/deviceai-labs/deviceai.git
cd deviceai
make setup                                          # download sherpa-onnx pre-built binaries
./gradlew :kotlin:core:compileDebugKotlinAndroid
./gradlew :kotlin:speech:compileDebugKotlinAndroid
./gradlew :kotlin:llm:compileDebugKotlinAndroid
```

No `--recursive` needed — C++ dependencies are fetched automatically by CMake FetchContent at build time.

---

## Benchmarks

| Device | SoC | Model | Audio | Inference | RTF |
|--------|-----|-------|-------|-----------|-----|
| Redmi Note 9 Pro | Snapdragon 720G | whisper-tiny.en | 5.4s | 746ms | **0.14x** |

> RTF < 1.0 = faster than real-time. 0.14x = ~7× faster than real-time.

---

## Roadmap

- [x] Kotlin SDK — speech, LLM, RAG, streaming
- [x] `DeviceAI` entry point + `CloudConfig` DSL
- [x] `ChatSession` — stateful multi-turn conversations
- [x] `sdk/deviceai-commons` — shared C++ layer with FetchContent
- [x] Backend integration — registration, manifest, telemetry
- [x] Auto hardware detection + SoC-based tier scoring
- [x] Staging backend live (`staging.api.deviceai.dev`)
- [ ] Swift SDK — native iOS/macOS package
- [ ] Flutter SDK
- [ ] React Native SDK
- [ ] OTA model download from R2 CDN
- [ ] Developer dashboard (Next.js)
- [ ] Tool calling / voice agents

---

## Sample App

```bash
# Open samples/androidApp/ in Android Studio and run on device/emulator
```

---

## Contributing

Issues and PRs welcome. Platform SDK contributions (`swift/`, `flutter/`, `react-native/`) are especially welcome.

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
