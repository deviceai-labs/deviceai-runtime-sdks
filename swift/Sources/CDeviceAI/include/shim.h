/**
 * CDeviceAI — C interop umbrella header for Swift SDK.
 *
 * Exposes the shared features layer C APIs:
 * - dai_stt_* (STT via whisper.cpp)
 * - dai_tts_* (TTS via sherpa-onnx)
 * - dai_llm_* (LLM via llama.cpp)
 */

#ifndef CDEVICEAI_SHIM_H
#define CDEVICEAI_SHIM_H

#include "dai_stt.h"
#include "dai_llm.h"
#include "dai_tts.h"

#endif /* CDEVICEAI_SHIM_H */
