set(BUILD_TESTING OFF)

include(FetchContent)

# ---- gtest (BSD-3-Clause)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.13.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---- ImGui (MIT)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.90
)
FetchContent_Populate(imgui)
include(imgui.cmake)
include(../cmake/imgui4lua.cmake)

# ---- luajit (MIT)
FetchContent_Declare(
  luajit
  GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
  GIT_TAG        8635cbabf3094c4d8bd00578c7d812bea87bb2d3
)
FetchContent_Populate(luajit)
include(luajit.cmake)

# ---- OpenGL
find_package(OpenGL REQUIRED GLOBAL)
add_library(OpenGL ALIAS OpenGL::GL)

# ---- SDL2 (zlib)
find_package(SDL2 REQUIRED GLOBAL)
add_library(SDL2 ALIAS SDL2::SDL2)

# ---- SQLite (public domain)
FetchContent_Declare(
  sqlite
  URL      https://www.sqlite.org/2023/sqlite-amalgamation-3430000.zip
  URL_HASH SHA256=bb5849ae4d7129c09d20596379a0b3f7b1ac59cf9998eba5ef283ea9b6c000a5
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_Populate(sqlite)
include(sqlite.cmake)

# ---- uvw (MIT)
FetchContent_Declare(
  uvw
  GIT_REPOSITORY https://github.com/skypjack/uvw.git
  GIT_TAG        v3.2.0_libuv_v1.46
  SYSTEM
)
set(BUILD_UVW_LIBS ON)
FetchContent_MakeAvailable(uvw)
