cmake_minimum_required(VERSION 3.22)
project(speech_jni)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to enable/disable TTS (requires ONNX Runtime)
option(SPEECHKMP_ENABLE_TTS "Enable Text-to-Speech (requires ONNX Runtime)" OFF)

# ═══════════════════════════════════════════════════════════════
#                        PATHS
# ═══════════════════════════════════════════════════════════════

set(WHISPER_DIR "${CMAKE_SOURCE_DIR}/../../../../../whisper.cpp")
set(PIPER_DIR "${CMAKE_SOURCE_DIR}/../../../../../piper")
set(ONNX_DIR "${CMAKE_SOURCE_DIR}/../../../../../onnxruntime")
set(ESPEAK_DIR "${CMAKE_SOURCE_DIR}/../../../../../espeak-ng")

# ═══════════════════════════════════════════════════════════════
#                      WHISPER.CPP (STT)
# ═══════════════════════════════════════════════════════════════

# Disable unnecessary features
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)  # No Metal on Android

if(EXISTS ${WHISPER_DIR}/CMakeLists.txt)
    add_subdirectory(${WHISPER_DIR} whisper-build EXCLUDE_FROM_ALL)
    set(WHISPER_FOUND TRUE)
else()
    message(WARNING "whisper.cpp not found at ${WHISPER_DIR}")
    set(WHISPER_FOUND FALSE)
endif()

# ═══════════════════════════════════════════════════════════════
#                         TTS (PIPER)
# ═══════════════════════════════════════════════════════════════

if(SPEECHKMP_ENABLE_TTS)
    # ONNX Runtime (pre-built for Android)
    if(ANDROID)
        set(ONNX_RUNTIME_LIB "${ONNX_DIR}/jni/${ANDROID_ABI}/libonnxruntime.so")
        set(ONNX_RUNTIME_INCLUDE "${ONNX_DIR}/headers")
    else()
        find_library(ONNX_RUNTIME_LIB onnxruntime HINTS ${ONNX_DIR}/lib)
        set(ONNX_RUNTIME_INCLUDE "${ONNX_DIR}/include")
    endif()

    if(NOT EXISTS "${ONNX_RUNTIME_LIB}")
        message(WARNING "ONNX Runtime not found. TTS will be disabled.")
        set(SPEECHKMP_ENABLE_TTS OFF)
    endif()
endif()

if(SPEECHKMP_ENABLE_TTS)
    # espeak-ng (embedded phonemization)
    set(USE_ASYNC OFF CACHE BOOL "" FORCE)
    set(USE_MBROLA OFF CACHE BOOL "" FORCE)
    set(USE_LIBPCAUDIO OFF CACHE BOOL "" FORCE)
    set(USE_KLATT OFF CACHE BOOL "" FORCE)
    set(USE_SPEECHPLAYER OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(EXTRA_cmn ON CACHE BOOL "" FORCE)
    set(EXTRA_ru ON CACHE BOOL "" FORCE)

    if(EXISTS ${ESPEAK_DIR}/CMakeLists.txt)
        add_subdirectory(${ESPEAK_DIR} espeak-build EXCLUDE_FROM_ALL)
        set(ESPEAK_FOUND TRUE)
    else()
        message(WARNING "espeak-ng not found at ${ESPEAK_DIR}")
        set(ESPEAK_FOUND FALSE)
        set(SPEECHKMP_ENABLE_TTS OFF)
    endif()
endif()

if(SPEECHKMP_ENABLE_TTS AND ESPEAK_FOUND)
    # Piper TTS library (espeak-ng is linked directly — piper-phonemize removed)
    if(EXISTS ${PIPER_DIR}/src/cpp/piper.cpp)
        add_library(piper STATIC
            ${PIPER_DIR}/src/cpp/piper.cpp
        )
        target_include_directories(piper PUBLIC
            ${PIPER_DIR}/src/cpp
        )
        target_include_directories(piper PRIVATE
            ${ONNX_RUNTIME_INCLUDE}
            ${ESPEAK_DIR}/src/include
        )
        target_link_libraries(piper
            espeak-ng
            ${ONNX_RUNTIME_LIB}
        )
        set(PIPER_FOUND TRUE)
    else()
        message(WARNING "piper not found at ${PIPER_DIR}")
        set(PIPER_FOUND FALSE)
        set(SPEECHKMP_ENABLE_TTS OFF)
    endif()
endif()

# ═══════════════════════════════════════════════════════════════
#                      JNI LIBRARY
# ═══════════════════════════════════════════════════════════════

if(ANDROID)
    find_library(log-lib log)
endif()

set(JNI_SOURCES deviceai_whisper_jni.cpp)

if(SPEECHKMP_ENABLE_TTS)
    list(APPEND JNI_SOURCES piper_jni.cpp)
endif()

add_library(speech_jni SHARED ${JNI_SOURCES})

target_include_directories(speech_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}
)

if(NOT SPEECHKMP_ENABLE_TTS)
    target_compile_definitions(speech_jni PRIVATE SPEECHKMP_STT_ONLY=1)
endif()

if(SPEECHKMP_ENABLE_TTS)
    target_include_directories(speech_jni PRIVATE
        ${PIPER_DIR}/src/cpp
        ${ONNX_RUNTIME_INCLUDE}
    )
endif()

if(WHISPER_FOUND)
    target_link_libraries(speech_jni whisper)
endif()

if(SPEECHKMP_ENABLE_TTS AND PIPER_FOUND)
    target_link_libraries(speech_jni piper)
endif()

if(ANDROID)
    target_link_libraries(speech_jni ${log-lib})
endif()

# ═══════════════════════════════════════════════════════════════
#                      COMPILE FLAGS
# ═══════════════════════════════════════════════════════════════

if(ANDROID)
    target_compile_options(speech_jni PRIVATE
        -fPIC
        -fvisibility=hidden
        -O3
    )
else()
    target_compile_options(speech_jni PRIVATE
        -fPIC
        -O3
    )
endif()
