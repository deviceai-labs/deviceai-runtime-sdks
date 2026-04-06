# sherpa-onnx backend (TTS + VAD)
#
# Android:  pre-built .so from sdk/deviceai-commons/prebuilt/sherpa-onnx/${ABI}/
# Desktop:  build from FetchContent source (onnxruntime downloads itself)
# iOS:      not yet supported (XCFramework path planned)

if(ANDROID)
    set(SHERPA_PREBUILT_DIR "${CMAKE_SOURCE_DIR}/prebuilt/sherpa-onnx/${ANDROID_ABI}")
    set(SHERPA_PREBUILT_SO "${SHERPA_PREBUILT_DIR}/libsherpa-onnx-c-api.so")

    if(EXISTS "${SHERPA_PREBUILT_SO}" AND DEFINED SHERPA_ONNX_HEADER_DIR)
        add_library(sherpa-onnx-c-api SHARED IMPORTED GLOBAL)
        set_target_properties(sherpa-onnx-c-api PROPERTIES
            IMPORTED_LOCATION "${SHERPA_PREBUILT_SO}"
        )
        set(DAI_SHERPA_FOUND TRUE PARENT_SCOPE)
        message(STATUS "[deviceai-commons] sherpa-onnx pre-built found for ${ANDROID_ABI}")
    else()
        set(DAI_SHERPA_FOUND FALSE PARENT_SCOPE)
        message(STATUS "[deviceai-commons] sherpa-onnx pre-built not found for ${ANDROID_ABI}")
        message(STATUS "  Run: sdk/deviceai-commons/scripts/download-sherpa-onnx.sh android")
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(DAI_SHERPA_FOUND FALSE PARENT_SCOPE)
    message(STATUS "[deviceai-commons] sherpa-onnx skipped on iOS")

elseif(DEFINED SHERPA_ONNX_HEADER_DIR AND EXISTS "${SHERPA_ONNX_HEADER_DIR}/CMakeLists.txt")
    # Desktop: build from source
    set(SHERPA_ONNX_ENABLE_PYTHON    OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TESTS     OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_CHECK     OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_JNI       OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_C_API     ON  CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS            OFF CACHE BOOL "" FORCE)

    add_subdirectory("${SHERPA_ONNX_HEADER_DIR}" sherpa-build EXCLUDE_FROM_ALL)
    set(DAI_SHERPA_FOUND TRUE PARENT_SCOPE)
    message(STATUS "[deviceai-commons] sherpa-onnx building from source (desktop)")
else()
    set(DAI_SHERPA_FOUND FALSE PARENT_SCOPE)
    message(STATUS "[deviceai-commons] sherpa-onnx not available — TTS disabled")
endif()
