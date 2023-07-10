cmake_minimum_required(VERSION 3.20)
project(nf7 C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

add_subdirectory(thirdparty EXCLUDE_FROM_ALL)
include(cmake/git_hash.cmake)

add_subdirectory(iface)
add_subdirectory(core)

add_executable(nf7)
target_sources(nf7 PRIVATE main.cc)
target_link_libraries(nf7 PRIVATE nf7_iface nf7_core)
