include(FetchContent)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 OLD)
endif()

if (NF7_STATIC)
  set(BUILD_SHARED_LIBS OFF)
else()
  set(BUILD_SHARED_LIBS ON)
endif()


# ---- ExprTk ----
# repository: https://github.com/ArashPartow/exprtk
# license   : MIT

FetchContent_Declare(
  exprtk
  URL "https://github.com/ArashPartow/exprtk/archive/refs/tags/0.0.1.zip"
)
FetchContent_Populate(exprtk)

add_library(exprtk INTERFACE)
target_include_directories(exprtk SYSTEM INTERFACE "${exprtk_SOURCE_DIR}")


# ---- FreeType ----
# repository: https://gitlab.freedesktop.org/freetype/freetype
# license   : The FreeType License

FetchContent_Declare(
  freetype
  URL "https://gitlab.freedesktop.org/freetype/freetype/-/archive/VER-2-12-1/freetype-VER-2-12-1.zip"
)

option(FT_DISABLE_ZLIB     ON)
option(FT_DISABLE_BZIP2    ON)
option(FT_DISABLE_PNG      ON)
option(FT_DISABLE_HARFBUZZ ON)
option(FT_DISABLE_BROTLI   ON)
option(FT_ENABLE_ERROR_STRINGS ON)
FetchContent_MakeAvailable(freetype)


# ---- GLEW ----
# repository: https://github.com/Perlmint/glew-cmake
# license   :  Modified BSD License, the Mesa 3-D License (MIT) and the Khronos License (MIT).

FetchContent_Declare(
  glew
  URL "https://github.com/Perlmint/glew-cmake/archive/refs/tags/glew-cmake-2.2.0.zip"
)
FetchContent_MakeAvailable(glew)

if (NF7_STATIC)
  add_library(glew ALIAS libglew_static)
else()
  add_library(glew ALIAS libglew_shared)
endif()


# ---- GLFW ----
# repository: https://github.com/glfw/glfw
# license   : zlib

FetchContent_Declare(
  glfw
  URL "https://github.com/glfw/glfw/archive/refs/tags/3.3.4.zip"
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)


# ---- ImGUI (docking branch) ----
# repository: https://github.com/ocornut/imgui/
# license   : MIT

FetchContent_Declare(
  imgui
  URL "https://github.com/ocornut/imgui/archive/9b0c26b0b2adae3ccf66dc9552fae4945d735a0c.zip"
)
FetchContent_Populate(imgui)

add_library(imgui STATIC)
target_sources(imgui
  PRIVATE
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_internal.h"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
  PUBLIC
    "${imgui_SOURCE_DIR}/imgui.h"
    "${imgui_SOURCE_DIR}/imstb_rectpack.h"
    "${imgui_SOURCE_DIR}/imstb_textedit.h"
    "${imgui_SOURCE_DIR}/imstb_truetype.h"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.h"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h"
)
target_include_directories(imgui SYSTEM
  PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${imgui_SOURCE_DIR}/misc/cpp"
)
target_link_libraries(imgui
  PRIVATE glfw
)


# ---- ImNodes ----
# repository: https://github.com/rokups/ImNodes
# license   : MIT

FetchContent_Declare(
  imnodes
  URL "https://github.com/rokups/ImNodes/archive/50f845875760517289b27ca265a9ad72057a644c.zip"
)
FetchContent_Populate(imnodes)

add_library(imnodes)
target_link_libraries(imnodes
  PRIVATE
    imgui
)
target_include_directories(imnodes SYSTEM
  PUBLIC
    "${imnodes_SOURCE_DIR}"
)
target_sources(imnodes
  PUBLIC
    "${imnodes_SOURCE_DIR}/ImNodes.h"
  PRIVATE
    "${imnodes_SOURCE_DIR}/ImNodes.cpp"
)


# ---- ImPlot ----
# repository: https://github.com/epezent/implot
# license   : MIT

FetchContent_Declare(
  implot
  URL "https://github.com/epezent/implot/archive/refs/tags/v0.14.zip"
)
FetchContent_Populate(implot)

add_library(implot)
target_link_libraries(implot
  PRIVATE
    imgui
)
target_include_directories(implot SYSTEM
  PUBLIC
    "${implot_SOURCE_DIR}"
)
target_sources(implot
  PUBLIC
    "${implot_SOURCE_DIR}/implot.h"
    "${implot_SOURCE_DIR}/implot_internal.h"
  PRIVATE
    "${implot_SOURCE_DIR}/implot.cpp"
    "${implot_SOURCE_DIR}/implot_items.cpp"
)


# ---- linalg.h ----
# repository: https://github.com/sgorsten/linalg
# license   : Unlicense

FetchContent_Declare(
  linalg_h
  URL "https://github.com/sgorsten/linalg/archive/a3e87da35e32b781a4b6c01cdd5efbe7ae51c737.zip"
)
FetchContent_Populate(linalg_h)

add_library(linalg.h INTERFACE)
target_include_directories(linalg.h SYSTEM
  INTERFACE .  "${linalg_h_SOURCE_DIR}"
)
target_sources(linalg.h
  INTERFACE
    "${linalg_h_SOURCE_DIR}/linalg.h"
    linalg.hh
)


# ---- luajit ----
# repository: https://github.com/LuaJIT/LuaJIT
# license   : MIT

FetchContent_Declare(
  luajit
  URL "https://github.com/LuaJIT/LuaJIT/archive/e3bae12fc0461cfa7e4bef3dfed2dad372e5da8d.zip"
)
FetchContent_Populate(luajit)

function (include_luajit)
  include(luajit.cmake)
endfunction()
include_luajit()


# ---- magic_enum ----
FetchContent_Declare(
  magic_enum
  URL "https://github.com/Neargye/magic_enum/archive/refs/tags/v0.8.1.zip"
)
FetchContent_MakeAvailable(magic_enum)


# ---- miniaudio ----
# repository: https://github.com/mackron/miniaudio
# license   : Unlicense

FetchContent_Declare(
  miniaudio
  URL "https://github.com/mackron/miniaudio/archive/4d813cfe23c28db165cce6785419fee9d2399766.zip"
)
FetchContent_Populate(miniaudio)

add_library(miniaudio)
target_include_directories(miniaudio PUBLIC SYSTEM ${miniaudio_SOURCE_DIR})
target_sources(miniaudio
  PUBLIC
    "${miniaudio_SOURCE_DIR}/miniaudio.h"
  PRIVATE
    miniaudio.c
)


# ---- source_location ----
add_library(source_location INTERFACE)
target_include_directories(source_location SYSTEM INTERFACE .)
target_sources(source_location INTERFACE source_location.hh)


# ---- stb ----
# repository: https://github.com/nothings/stb
# license   : Unlicense
FetchContent_Declare(
  stb
  URL "https://github.com/nothings/stb/archive/8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55.zip"
)
FetchContent_Populate(stb)

add_library(stb)
target_include_directories(stb PUBLIC SYSTEM ${stb_SOURCE_DIR})
target_sources(stb
  PUBLIC
    ${stb_SOURCE_DIR}/stb_image.h
  PRIVATE
    stb.c
)


# ---- Tracy ----
# repository: https://github.com/wolfpld/tracy
# license   : 3-clause BSD

FetchContent_Declare(
  tracy
  URL "https://github.com/wolfpld/tracy/archive/refs/tags/v0.9.zip"
)
option(TRACY_ENABLE "" ${NF7_PROFILE})
FetchContent_MakeAvailable(tracy)


# ---- yaml-cpp ----
# repository: https://github.com/jbeder/yaml-cpp
# license   : MIT

FetchContent_Declare(
  yaml-cpp
  URL "https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.7.0.zip"
)
FetchContent_MakeAvailable(yaml-cpp)


# ---- yas  ----
# repository: https://github.com/niXman/yas
# license   : Boost

FetchContent_Declare(
  yas
  URL "https://github.com/niXman/yas/archive/refs/tags/7.1.0.zip"
)
FetchContent_Populate(yas)

add_library(yas INTERFACE)
target_include_directories(yas SYSTEM INTERFACE "${yas_SOURCE_DIR}/include")
