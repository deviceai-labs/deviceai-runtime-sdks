cmake_minimum_required(VERSION 3.22)
project(llm_jni_android)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LLAMA_DIR "${CMAKE_SOURCE_DIR}/../../../../llama.cpp")

set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(GGML_METAL           OFF CACHE BOOL "" FORCE)

if(EXISTS ${LLAMA_DIR}/CMakeLists.txt)
    add_subdirectory(${LLAMA_DIR} llama-build EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "llama.cpp not found at ${LLAMA_DIR}")
endif()

find_library(log-lib log)

add_library(llm_jni SHARED
    ${CMAKE_SOURCE_DIR}/../../src/commonMain/cpp/llm_jni.cpp
)

target_include_directories(llm_jni PRIVATE
    ${CMAKE_SOURCE_DIR}/../../src/commonMain/cpp
    ${LLAMA_DIR}/include
    ${LLAMA_DIR}
)

target_link_libraries(llm_jni llama ${log-lib})

target_compile_options(llm_jni PRIVATE -fPIC -fvisibility=hidden -O3)
