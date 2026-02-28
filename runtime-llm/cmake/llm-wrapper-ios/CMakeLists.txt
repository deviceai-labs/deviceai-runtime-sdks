cmake_minimum_required(VERSION 3.22)
project(llm_static)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ═══════════════════════════════════════════════════════════════
#                           PATHS
# ═══════════════════════════════════════════════════════════════

set(LLAMA_DIR "${CMAKE_SOURCE_DIR}/../../../../llama.cpp")
set(BRIDGE_DIR "${CMAKE_SOURCE_DIR}/../../src/iosMain/cpp")

# ═══════════════════════════════════════════════════════════════
#                         llama.cpp
# ═══════════════════════════════════════════════════════════════

set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(GGML_METAL           ON  CACHE BOOL "" FORCE)  # Metal acceleration on iOS

if(EXISTS ${LLAMA_DIR}/CMakeLists.txt)
    add_subdirectory(${LLAMA_DIR} llama-build EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "llama.cpp not found at ${LLAMA_DIR}")
endif()

# ═══════════════════════════════════════════════════════════════
#                     Bridge static library
# ═══════════════════════════════════════════════════════════════

add_library(llm_static STATIC
    ${BRIDGE_DIR}/llm_ios.cpp
)

target_include_directories(llm_static PRIVATE
    ${LLAMA_DIR}/include
    ${LLAMA_DIR}
    ${CMAKE_SOURCE_DIR}/../../src/iosMain/c_interop/include
)

target_link_libraries(llm_static llama)

target_compile_options(llm_static PRIVATE
    -fvisibility=hidden
    -O3
)
