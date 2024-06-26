cmake_minimum_required(VERSION 3.27)

project(nf7 C)

set(CMAKE_C_STANDARD              23)
set(CMAKE_C_STANDARD_REQUIRED     ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(tool/meta.cmake)

# ---- thirdparty import
add_subdirectory(thirdparty EXCLUDE_FROM_ALL)

# ---- common config
add_library(nf7config INTERFACE)
target_include_directories(nf7config
  INTERFACE
    ${PROJECT_SOURCE_DIR}
    ${NF7_GENERATED_DIR}
)
target_compile_options(nf7config INTERFACE
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4
    $<$<CONFIG:Debug>:/WX>
  >
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
    -Wall -Wextra -Wpedantic
    -Wno-gnu-zero-variadic-macro-arguments
    $<$<CONFIG:Debug>:-Werror>
  >
)

# ---- test library
add_subdirectory(test EXCLUDE_FROM_ALL)

# ---- util library
add_subdirectory(util)

# ---- interface library
add_library(nf7if INTERFACE)
target_sources(nf7if INTERFACE nf7.h)
target_link_libraries(nf7if INTERFACE nf7config nf7util uv)

# ---- core library
add_subdirectory(core)

# ---- main executable
add_executable(nf7)
target_sources(nf7 PRIVATE main.c)
target_link_libraries(nf7
  PRIVATE
    nf7if
    nf7core
)
