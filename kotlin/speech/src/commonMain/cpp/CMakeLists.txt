cmake_minimum_required(VERSION 3.22)
project(speech_jni)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ═══════════════════════════════════════════════════════════════
#                        PATHS
# ═══════════════════════════════════════════════════════════════

set(WHISPER_DIR      "${CMAKE_SOURCE_DIR}/../../../../../whisper.cpp")
set(SHERPA_ONNX_DIR  "${CMAKE_SOURCE_DIR}/../../../../../sherpa-onnx")
set(ENGINE_DIR       "${CMAKE_SOURCE_DIR}/../../../../../cpp/speech")

# ═══════════════════════════════════════════════════════════════
#                      WHISPER.CPP (STT)
# ═══════════════════════════════════════════════════════════════

set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GGML_METAL             OFF CACHE BOOL "" FORCE)  # No Metal on Android

if(EXISTS ${WHISPER_DIR}/CMakeLists.txt)
    add_subdirectory(${WHISPER_DIR} whisper-build EXCLUDE_FROM_ALL)
    set(WHISPER_FOUND TRUE)
else()
    message(WARNING "whisper.cpp not found at ${WHISPER_DIR}")
    set(WHISPER_FOUND FALSE)
endif()

# ═══════════════════════════════════════════════════════════════
#                    TTS + VAD (SHERPA-ONNX)
# ═══════════════════════════════════════════════════════════════

set(SHERPA_ONNX_ENABLE_TESTS    OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PYTHON   OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_CHECK    OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_JNI      OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_C_API    ON  CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_BINARY   OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_TTS      ON  CACHE BOOL "" FORCE)

if(EXISTS ${SHERPA_ONNX_DIR}/CMakeLists.txt)
    add_subdirectory(${SHERPA_ONNX_DIR} sherpa-onnx-build EXCLUDE_FROM_ALL)
    set(SHERPA_ONNX_FOUND TRUE)
else()
    message(WARNING "sherpa-onnx not found at ${SHERPA_ONNX_DIR}. TTS and neural VAD will be unavailable.")
    set(SHERPA_ONNX_FOUND FALSE)
endif()

# ═══════════════════════════════════════════════════════════════
#                      JNI LIBRARY
# ═══════════════════════════════════════════════════════════════

if(ANDROID)
    find_library(log-lib log)
endif()

add_library(speech_jni SHARED
    whisper_jni.cpp
    piper_jni.cpp
    ${ENGINE_DIR}/deviceai_speech_engine.cpp
)

target_include_directories(speech_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ENGINE_DIR}
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}
)

if(SHERPA_ONNX_FOUND)
    target_include_directories(speech_jni PRIVATE ${SHERPA_ONNX_DIR})
    target_compile_definitions(speech_jni PRIVATE HAVE_SHERPA_ONNX=1)
    target_link_libraries(speech_jni sherpa-onnx-c-api)
endif()

if(WHISPER_FOUND)
    target_link_libraries(speech_jni whisper)
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
