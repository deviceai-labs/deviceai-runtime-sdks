include(FetchContent)

# ═══════════════════════════════════════════════════════════════
#                     whisper.cpp (STT)
# ═══════════════════════════════════════════════════════════════

if(DAI_ENABLE_STT)
    set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    if(ANDROID)
        set(GGML_METAL     OFF CACHE BOOL "" FORCE)
        set(WHISPER_COREML OFF CACHE BOOL "" FORCE)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR APPLE)
        set(GGML_METAL              ON CACHE BOOL "" FORCE)
        set(GGML_ACCELERATE         ON CACHE BOOL "" FORCE)
        set(WHISPER_COREML          ON CACHE BOOL "" FORCE)
        set(WHISPER_COREML_ALLOW_FALLBACK ON CACHE BOOL "" FORCE)
    else()
        set(GGML_METAL OFF CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(whisper_cpp
        GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
        GIT_TAG        ${WHISPER_CPP_COMMIT}
        GIT_SHALLOW    FALSE
    )
    FetchContent_MakeAvailable(whisper_cpp)

    set(DAI_WHISPER_FOUND TRUE)
    message(STATUS "[deviceai-commons] whisper.cpp ${WHISPER_CPP_VERSION} fetched")
endif()

# ═══════════════════════════════════════════════════════════════
#                     llama.cpp (LLM)
# ═══════════════════════════════════════════════════════════════

if(DAI_ENABLE_LLM)
    set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

    # CRITICAL: Force static linking to avoid ggml symbol collision with whisper.cpp.
    # speech_jni and deviceai_llm_jni are separate .so files — static linking embeds
    # ggml into each, preventing dlopen failures from symbol version mismatch.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(GGML_SHARED       OFF CACHE BOOL "" FORCE)

    if(ANDROID)
        set(GGML_METAL OFF CACHE BOOL "" FORCE)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR APPLE)
        set(GGML_METAL ON CACHE BOOL "" FORCE)
    else()
        set(GGML_METAL OFF CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(llama_cpp
        GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
        GIT_TAG        ${LLAMA_CPP_COMMIT}
        GIT_SHALLOW    FALSE
    )
    FetchContent_MakeAvailable(llama_cpp)

    set(DAI_LLAMA_FOUND TRUE)
    message(STATUS "[deviceai-commons] llama.cpp ${LLAMA_CPP_VERSION} fetched")
endif()

# ═══════════════════════════════════════════════════════════════
#              sherpa-onnx (TTS/VAD — headers only)
# ═══════════════════════════════════════════════════════════════

if(DAI_ENABLE_TTS)
    FetchContent_Declare(sherpa_onnx
        GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
        GIT_TAG        ${SHERPA_ONNX_COMMIT}
        GIT_SHALLOW    FALSE
    )
    # Populate but do NOT build — we only need the C API headers.
    # Android uses pre-built .so binaries; desktop builds from source separately.
    FetchContent_GetProperties(sherpa_onnx)
    if(NOT sherpa_onnx_POPULATED)
        FetchContent_Populate(sherpa_onnx)
    endif()
    set(SHERPA_ONNX_HEADER_DIR "${sherpa_onnx_SOURCE_DIR}" CACHE PATH "" FORCE)

    message(STATUS "[deviceai-commons] sherpa-onnx ${SHERPA_ONNX_VERSION} headers fetched")
endif()
