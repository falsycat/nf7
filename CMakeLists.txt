cmake_minimum_required(VERSION 3.18)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# ---- configuration ----
project(nf7 CXX)

option(NF7_STATIC "link all libs statically" ON)

set(NF7_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include/generated")

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(NF7_CXX_FLAGS
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
    -Wall -Werror -pedantic-errors -Wextra -Wconversion -Wsign-conversion>
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
    -Wno-overloaded-virtual>
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4 /WX>
)

add_subdirectory(thirdparty EXCLUDE_FROM_ALL)


# ---- application ----
add_executable(nf7)
target_compile_options(nf7 PRIVATE ${NF7_CXX_FLAGS})
target_include_directories(nf7 PRIVATE . "${PROJECT_BINARY_DIR}/include")

target_compile_definitions(nf7
  PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS

    $<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>
    $<$<PLATFORM_ID:Darwin>:_GNU_SOURCE>
)
target_sources(nf7
  PRIVATE
    main.cc
    nf7.cc
    nf7.hh

    common/dir.hh
    common/queue.hh
    common/type_info.hh
    common/yas.hh

    file/system_dir.cc
)
target_link_libraries(nf7
  PRIVATE
    glew
    glfw
    imgui
    imnodes
    implot
    linalg.h
    luajit
    source_location
    yas
)
