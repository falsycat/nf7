include(FetchContent)


# ---- libuv (MIT)
FetchContent_Declare(
  libuv
  GIT_REPOSITORY https://github.com/libuv/libuv.git
  GIT_TAG        v1.47.0
)
FetchContent_MakeAvailable(libuv)

# ---- LuaJIT (MIT)
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
