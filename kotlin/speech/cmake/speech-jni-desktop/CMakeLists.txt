cmake_minimum_required(VERSION 3.18)
project(speech_jni_desktop)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ═══════════════════════════════════════════════════════════════
#                        PATHS
# ═══════════════════════════════════════════════════════════════

set(WHISPER_DIR "${CMAKE_SOURCE_DIR}/../../../../whisper.cpp")
set(PIPER_DIR "${CMAKE_SOURCE_DIR}/../../../../piper")
set(ONNX_DIR "${CMAKE_SOURCE_DIR}/../../../../onnxruntime")
set(ESPEAK_DIR "${CMAKE_SOURCE_DIR}/../../../../espeak-ng")
set(JNI_CPP_DIR "${PROJECT_SOURCE_DIR}/../../src/commonMain/cpp")

# ═══════════════════════════════════════════════════════════════
#                      WHISPER.CPP
# ═══════════════════════════════════════════════════════════════

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Enable Metal and Core ML on macOS
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_ACCELERATE ON CACHE BOOL "" FORCE)
    set(WHISPER_COREML ON CACHE BOOL "" FORCE)
    set(WHISPER_COREML_ALLOW_FALLBACK ON CACHE BOOL "" FORCE)
else()
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
    set(WHISPER_COREML OFF CACHE BOOL "" FORCE)
endif()

if(EXISTS ${WHISPER_DIR}/CMakeLists.txt)
    add_subdirectory(${WHISPER_DIR} whisper-build EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "whisper.cpp not found at ${WHISPER_DIR}")
endif()

# ═══════════════════════════════════════════════════════════════
#                         PIPER
# ═══════════════════════════════════════════════════════════════

# espeak-ng
set(USE_ASYNC OFF CACHE BOOL "" FORCE)
set(USE_MBROLA OFF CACHE BOOL "" FORCE)
set(USE_LIBPCAUDIO OFF CACHE BOOL "" FORCE)
set(USE_KLATT OFF CACHE BOOL "" FORCE)
set(USE_SPEECHPLAYER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(EXISTS ${ESPEAK_DIR}/CMakeLists.txt)
    add_subdirectory(${ESPEAK_DIR} espeak-build EXCLUDE_FROM_ALL)
endif()

# Find ONNX Runtime
if(APPLE)
    # macOS
    find_library(ONNX_RUNTIME_LIB onnxruntime
        HINTS ${ONNX_DIR}/lib /usr/local/lib /opt/homebrew/lib
    )
elseif(WIN32)
    # Windows
    find_library(ONNX_RUNTIME_LIB onnxruntime
        HINTS ${ONNX_DIR}/lib ${ONNX_DIR}/bin
    )
else()
    # Linux
    find_library(ONNX_RUNTIME_LIB onnxruntime
        HINTS ${ONNX_DIR}/lib /usr/lib /usr/local/lib
    )
endif()

# Piper library
if(EXISTS ${PIPER_DIR}/src/cpp/piper.cpp)
    add_library(piper STATIC
        ${PIPER_DIR}/src/cpp/piper.cpp
        ${PIPER_DIR}/src/cpp/phonemize.cpp
        ${PIPER_DIR}/src/cpp/phoneme_ids.cpp
    )
    target_include_directories(piper PRIVATE
        ${PIPER_DIR}/src/cpp
        ${ONNX_DIR}/include
        ${ESPEAK_DIR}/src/include
    )
    if(TARGET espeak-ng)
        target_link_libraries(piper espeak-ng)
    endif()
    if(ONNX_RUNTIME_LIB)
        target_link_libraries(piper ${ONNX_RUNTIME_LIB})
    endif()
endif()

# ═══════════════════════════════════════════════════════════════
#                       JNI SETUP
# ═══════════════════════════════════════════════════════════════

find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})

# ═══════════════════════════════════════════════════════════════
#                    JNI SHARED LIBRARY
# ═══════════════════════════════════════════════════════════════

add_library(speech_jni SHARED
    ${JNI_CPP_DIR}/deviceai_whisper_jni.cpp
    ${JNI_CPP_DIR}/piper_jni.cpp
)

target_include_directories(speech_jni PRIVATE
    ${JNI_CPP_DIR}
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}
)

if(EXISTS ${PIPER_DIR}/src/cpp)
    target_include_directories(speech_jni PRIVATE
        ${PIPER_DIR}/src/cpp
        ${ONNX_DIR}/include
    )
endif()

# Link dependencies
target_link_libraries(speech_jni whisper)

if(TARGET piper)
    target_link_libraries(speech_jni piper)
endif()

# Platform-specific linking
if(APPLE)
    target_link_libraries(speech_jni
        "-framework Accelerate"
        "-framework Metal"
        "-framework MetalKit"
        "-framework CoreML"
        "-framework Foundation"
    )
endif()

find_package(Threads REQUIRED)
target_link_libraries(speech_jni Threads::Threads)

# Output directory
set_target_properties(speech_jni PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ═══════════════════════════════════════════════════════════════
#                    COMPILE FLAGS
# ═══════════════════════════════════════════════════════════════

target_compile_options(speech_jni PRIVATE
    -fPIC
    -O3
)

if(APPLE)
    target_compile_options(speech_jni PRIVATE
        -fvisibility=hidden
    )
endif()
