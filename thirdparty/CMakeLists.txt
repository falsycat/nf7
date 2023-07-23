include(FetchContent)

# ---- luajit (MIT)
FetchContent_Declare(
  luajit
  GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
  GIT_TAG        8635cbabf3094c4d8bd00578c7d812bea87bb2d3
)
FetchContent_Populate(luajit)
include(luajit.cmake)

# ---- gtest (BSD-3-Clause)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.13.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
