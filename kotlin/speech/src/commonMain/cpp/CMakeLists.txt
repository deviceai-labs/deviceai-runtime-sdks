cmake_minimum_required(VERSION 3.22)
project(speech_jni)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ═══════════════════════════════════════════════════════════════
#                        PATHS
# ═══════════════════════════════════════════════════════════════

set(WHISPER_DIR    "${CMAKE_SOURCE_DIR}/../../../../../third_party/whisper.cpp")
set(SHERPA_DIR     "${CMAKE_SOURCE_DIR}/../../../../../third_party/sherpa-onnx")

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
#                    TTS (SHERPA-ONNX)
# ═══════════════════════════════════════════════════════════════

if(EXISTS ${SHERPA_DIR}/CMakeLists.txt)
    # Build sherpa-onnx as a static library; disable unused extras
    set(SHERPA_ONNX_ENABLE_PYTHON       OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TESTS        OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_CHECK        OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PORTAUDIO    OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_JNI          OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_C_API        ON  CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS               OFF CACHE BOOL "" FORCE)

    add_subdirectory(${SHERPA_DIR} sherpa-build EXCLUDE_FROM_ALL)
    set(SHERPA_FOUND TRUE)
    message(STATUS "sherpa-onnx found — TTS enabled")
else()
    message(STATUS "sherpa-onnx not found at ${SHERPA_DIR} — TTS will use no-op stubs")
    set(SHERPA_FOUND FALSE)
endif()

# ═══════════════════════════════════════════════════════════════
#                      JNI LIBRARY
# ═══════════════════════════════════════════════════════════════

if(ANDROID)
    find_library(log-lib log)
endif()

add_library(speech_jni SHARED
    deviceai_whisper_jni.cpp
    deviceai_tts_jni.cpp
)

target_include_directories(speech_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}
)

if(SHERPA_FOUND)
    target_compile_definitions(speech_jni PRIVATE HAVE_SHERPA_ONNX=1)
    target_include_directories(speech_jni PRIVATE ${SHERPA_DIR})
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
