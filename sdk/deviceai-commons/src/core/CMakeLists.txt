add_library(deviceai_core STATIC
    src/session_store.cpp
    src/backend_client.cpp
    src/telemetry_engine.cpp
)

target_include_directories(deviceai_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils
)

target_compile_options(deviceai_core PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
    $<$<CONFIG:Release>:-O2>
)

find_package(Threads REQUIRED)
target_link_libraries(deviceai_core PRIVATE Threads::Threads)
