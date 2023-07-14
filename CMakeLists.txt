cmake_minimum_required(VERSION 3.20)
project(nf7 C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

# all targets should link to this to use common compile options
add_library(nf7_config INTERFACE EXCLUDE_FROM_ALL)
target_compile_options(nf7_config INTERFACE
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4
    $<$<CONFIG:Debug>:/WX>
  >
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-Werror>
  >
)

# include thirdparty libs and external scripts
add_subdirectory(thirdparty EXCLUDE_FROM_ALL)
include(cmake/git_hash.cmake)

# add main targets
add_subdirectory(iface)
add_subdirectory(core)

add_executable(nf7)
target_sources(nf7 PRIVATE main.cc)
target_link_libraries(nf7
    PRIVATE
      nf7_config
      nf7_iface
      nf7_core
)
