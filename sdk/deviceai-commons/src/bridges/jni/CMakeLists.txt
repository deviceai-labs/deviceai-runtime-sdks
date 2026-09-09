# ═══════════════════════════════════════════════════════════════
#            DeviceAI JNI Bridge Libraries (Android / JVM)
#
# JNI bridges are thin wrappers — all inference logic lives in
# src/features/{stt,tts,llm}/. Bridges only do JNI type conversion.
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

    # Link feature engines (contains all inference logic)
    if(DAI_ENABLE_STT AND TARGET dai_stt)
        target_link_libraries(speech_jni dai_stt)
    endif()
    if(DAI_ENABLE_TTS AND TARGET dai_tts)
        target_link_libraries(speech_jni dai_tts)
    endif()
    # Model tarball extraction (sherpa-onnx voices ship as .tar.bz2)
    target_link_libraries(speech_jni dai_archive)

    # Direct engine headers still needed for JNI-specific streaming code
    if(DAI_WHISPER_FOUND AND TARGET whisper)
        target_include_directories(speech_jni PRIVATE
            ${whisper_cpp_SOURCE_DIR}/include
            ${whisper_cpp_SOURCE_DIR}
        )
    endif()
    if(DAI_SHERPA_FOUND)
        target_compile_definitions(speech_jni PRIVATE HAVE_SHERPA_ONNX=1)
        target_include_directories(speech_jni PRIVATE ${SHERPA_ONNX_HEADER_DIR})
    endif()

    if(ANDROID)
        target_link_libraries(speech_jni ${log-lib})
    endif()

    target_compile_options(speech_jni PRIVATE -fPIC -fvisibility=hidden -O3)
endif()

# ── deviceai_llm_jni (LLM) ──────────────────────────────────

if(DAI_ENABLE_LLM AND TARGET dai_llm)
    add_library(deviceai_llm_jni SHARED deviceai_llm_jni.cpp)

    target_include_directories(deviceai_llm_jni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/features/llm
    )

    if(NOT ANDROID)
        target_include_directories(deviceai_llm_jni PRIVATE ${JNI_INCLUDE_DIRS})
    endif()

    target_link_libraries(deviceai_llm_jni dai_llm)

    if(ANDROID)
        target_link_libraries(deviceai_llm_jni ${log-lib})
    endif()

    target_compile_options(deviceai_llm_jni PRIVATE -fPIC -fvisibility=hidden -O3)
endif()

# ── deviceai_core_jni (Backend/Telemetry) ────────────────────

if(DAI_ENABLE_CORE AND TARGET deviceai_core)
    add_library(deviceai_core_jni SHARED dai_core_jni.cpp)

    target_include_directories(deviceai_core_jni PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core/include
    )

    if(NOT ANDROID)
        target_include_directories(deviceai_core_jni PRIVATE ${JNI_INCLUDE_DIRS})
    endif()

    target_link_libraries(deviceai_core_jni PRIVATE deviceai_core)

    if(ANDROID)
        target_link_libraries(deviceai_core_jni PRIVATE ${log-lib})
    endif()

    find_package(Threads REQUIRED)
    target_link_libraries(deviceai_core_jni PRIVATE Threads::Threads)

    target_compile_options(deviceai_core_jni PRIVATE -fPIC -fvisibility=hidden $<$<CONFIG:Release>:-O2>)
endif()
