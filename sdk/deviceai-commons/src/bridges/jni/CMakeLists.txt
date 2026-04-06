# ═══════════════════════════════════════════════════════════════
#            DeviceAI JNI Bridge Libraries (Android / JVM)
# ═══════════════════════════════════════════════════════════════

if(ANDROID)
    find_library(log-lib log)
else()
    find_package(JNI REQUIRED)
endif()

# ── speech_jni (STT + TTS) ──────────────────────────────────

if(DAI_ENABLE_STT OR DAI_ENABLE_TTS)
    add_library(speech_jni SHARED
        deviceai_whisper_jni.cpp
        deviceai_tts_jni.cpp
    )

    target_include_directories(speech_jni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(NOT ANDROID)
        target_include_directories(speech_jni PRIVATE ${JNI_INCLUDE_DIRS})
    endif()

    # whisper.cpp (STT)
    if(DAI_WHISPER_FOUND AND TARGET whisper)
        target_include_directories(speech_jni PRIVATE
            ${whisper_cpp_SOURCE_DIR}/include
            ${whisper_cpp_SOURCE_DIR}
        )
        target_link_libraries(speech_jni whisper)
    endif()

    # sherpa-onnx (TTS)
    if(DAI_SHERPA_FOUND)
        target_compile_definitions(speech_jni PRIVATE HAVE_SHERPA_ONNX=1)
        target_include_directories(speech_jni PRIVATE ${SHERPA_ONNX_HEADER_DIR})
        target_link_libraries(speech_jni sherpa-onnx-c-api)
    endif()

    if(ANDROID)
        target_link_libraries(speech_jni ${log-lib})
    endif()

    target_compile_options(speech_jni PRIVATE -fPIC -fvisibility=hidden -O3)
endif()

# ── deviceai_llm_jni (LLM) ──────────────────────────────────

if(DAI_ENABLE_LLM AND DAI_LLAMA_FOUND AND TARGET llama)
    add_library(deviceai_llm_jni SHARED deviceai_llm_jni.cpp)

    target_include_directories(deviceai_llm_jni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${llama_cpp_SOURCE_DIR}/include
        ${llama_cpp_SOURCE_DIR}
    )

    if(NOT ANDROID)
        target_include_directories(deviceai_llm_jni PRIVATE ${JNI_INCLUDE_DIRS})
    endif()

    target_link_libraries(deviceai_llm_jni llama)

    if(ANDROID)
        target_link_libraries(deviceai_llm_jni ${log-lib})
    endif()

    target_compile_options(deviceai_llm_jni PRIVATE -fPIC -fvisibility=hidden -O3)
endif()

# ── deviceai_core_jni (Backend/Telemetry) ────────────────────
# Built when DAI_ENABLE_CORE=ON and the core static lib is available.
# Source: dai_core_jni.cpp (added when backend integration merges)
