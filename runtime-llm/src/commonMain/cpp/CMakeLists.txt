cmake_minimum_required(VERSION 3.22)
project(llm_jni)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ═══════════════════════════════════════════════════════════════
#                           PATHS
# ═══════════════════════════════════════════════════════════════

set(LLAMA_DIR "${CMAKE_SOURCE_DIR}/../../../../llama.cpp")

# ═══════════════════════════════════════════════════════════════
#                         llama.cpp
# ═══════════════════════════════════════════════════════════════

set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(GGML_METAL           OFF CACHE BOOL "" FORCE)  # No Metal on Android/JVM

if(EXISTS ${LLAMA_DIR}/CMakeLists.txt)
    add_subdirectory(${LLAMA_DIR} llama-build EXCLUDE_FROM_ALL)
    set(LLAMA_FOUND TRUE)
else()
    message(WARNING "llama.cpp not found at ${LLAMA_DIR}")
    set(LLAMA_FOUND FALSE)
endif()

# ═══════════════════════════════════════════════════════════════
#                       JNI LIBRARY
# ═══════════════════════════════════════════════════════════════

if(ANDROID)
    find_library(log-lib log)
endif()

add_library(llm_jni SHARED llm_jni.cpp)

target_include_directories(llm_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLAMA_DIR}/include
    ${LLAMA_DIR}
)

if(LLAMA_FOUND)
    target_link_libraries(llm_jni llama)
endif()

if(ANDROID)
    target_link_libraries(llm_jni ${log-lib})
endif()

# ═══════════════════════════════════════════════════════════════
#                      COMPILE FLAGS
# ═══════════════════════════════════════════════════════════════

target_compile_options(llm_jni PRIVATE
    -fPIC
    -fvisibility=hidden
    -O3
)
