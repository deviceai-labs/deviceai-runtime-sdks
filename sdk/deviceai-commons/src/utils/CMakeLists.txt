# Shared utilities — archive extraction (tar.bz2) for model tarballs.
add_library(dai_archive STATIC archive_extract.c)
target_include_directories(dai_archive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dai_archive PUBLIC dai_bz2)
target_compile_options(dai_archive PRIVATE -fPIC -O2)
