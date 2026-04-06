cmake_minimum_required(VERSION 3.22)
project(deviceai_commons VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ──────────────────────────────────────────────────
option(DAI_BUILD_JNI     "Build JNI bridge shared libraries (Android/JVM)" ON)
option(DAI_BUILD_IOS     "Build iOS static bridge libraries"               OFF)
option(DAI_ENABLE_STT    "Enable STT backend (whisper.cpp)"                ON)
option(DAI_ENABLE_TTS    "Enable TTS backend (sherpa-onnx)"                ON)
option(DAI_ENABLE_LLM    "Enable LLM backend (llama.cpp)"                  ON)
option(DAI_ENABLE_CORE   "Enable core infrastructure (session, telemetry)" OFF)

# ── Load pinned dependency versions from VERSIONS file ───────
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSIONS" _version_lines)
foreach(_line ${_version_lines})
    if(_line MATCHES "^([A-Z_]+)=(.+)$")
        set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE STRING "" FORCE)
    endif()
endforeach()

# ── Fetch third-party dependencies ───────────────────────────
include(cmake/FetchDependencies.cmake)

# ── Backend wrappers ─────────────────────────────────────────
if(DAI_ENABLE_STT)
    add_subdirectory(src/backends/whisper)
endif()
if(DAI_ENABLE_TTS)
    add_subdirectory(src/backends/sherpa_onnx)
endif()
if(DAI_ENABLE_LLM)
    add_subdirectory(src/backends/llamacpp)
endif()

# ── Core infrastructure ──────────────────────────────────────
if(DAI_ENABLE_CORE)
    add_subdirectory(src/core)
endif()

# ── Platform bridges ─────────────────────────────────────────
if(DAI_BUILD_JNI)
    add_subdirectory(src/bridges/jni)
endif()
if(DAI_BUILD_IOS)
    add_subdirectory(src/bridges/ios)
endif()
