cmake_minimum_required(VERSION 3.18)
cmake_policy(SET CMP0077 NEW)

# ---- configuration ----
project(nf7 C CXX)

option(NF7_STATIC          "link all libs statically" ON)
option(NF7_SANITIZE_THREAD "use thread sanitizer"     OFF)
option(NF7_SANITIZE        "use various sanitizer"    OFF)
option(NF7_PROFILE         "profiler"                 OFF)

if (NF7_SANITIZE_THREAD AND NF7_PROFILE)
  message(FATAL_ERROR "NF7_SANITIZE_THREAD cannot be enabled with NF7_PROFILE")
endif()
if (NF7_SANITIZE AND NF7_SANITIZE_THREAD)
  message(FATAL_ERROR "NF7_SANITIZE_THREAD cannot be enabled with NF7_SANITIZE")
endif()

set(NF7_OPTIONS_WARNING
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
    -Wall -Werror -pedantic-errors -Wextra -Wconversion -Wsign-conversion>
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
    -Wno-overloaded-virtual>
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4 /WX /Zc:__cplusplus /external:anglebrackets /external:W0>
)

if (NF7_SANITIZE)
  set(NF7_OPTIONS_SANITIZE
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:GNU>:
      -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-omit-frame-pointer>>
  )
endif()
if (NF7_SANITIZE_THREAD)
  set(NF7_OPTIONS_SANITIZE
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:GNU>:
      -fsanitize=thread -fno-omit-frame-pointer>>
  )
endif()

set(NF7_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include/generated")
file(MAKE_DIRECTORY "${NF7_GENERATED_INCLUDE_DIR}")

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_subdirectory(thirdparty EXCLUDE_FROM_ALL)


# ---- application ----
add_executable(nf7)
target_include_directories(nf7 PRIVATE . "${PROJECT_BINARY_DIR}/include")
target_compile_options(nf7 PRIVATE
  ${NF7_OPTIONS_WARNING}
  ${NF7_OPTIONS_SANITIZE}
  $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
target_link_options(nf7 PRIVATE
  ${NF7_OPTIONS_SANITIZE}
)
target_compile_definitions(nf7
  PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS

    $<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>
    $<$<PLATFORM_ID:Darwin>:_GNU_SOURCE>
)
target_sources(nf7
  PRIVATE
    init.hh
    main.cc
    nf7.cc
    nf7.hh
    theme.hh
    version.hh

    common/aggregate_command.hh
    common/aggregate_promise.hh
    common/audio_queue.hh
    common/config.hh
    common/context_owner.hh
    common/dir.hh
    common/dir_item.hh
    common/dll.hh
    common/factory.hh
    common/file_base.hh
    common/font_queue.hh
    common/future.hh
    common/generic_config.hh
    common/generic_context.hh
    common/generic_dir.hh
    common/generic_history.hh
    common/generic_memento.hh
    common/generic_type_info.hh
    common/generic_watcher.hh
    common/gl_enum.hh
    common/gl_fence.hh
    common/gl_obj.hh
    common/gl_obj.cc
    common/gl_shader_preproc.hh
    common/gui.hh
    common/gui.cc
    common/gui_dnd.hh
    common/gui_timeline.hh
    common/gui_timeline.cc
    common/gui_window.hh
    common/gui_window.cc
    common/history.hh
    common/life.hh
    common/logger.hh
    common/logger_ref.hh
    common/luajit.hh
    common/luajit.cc
    common/luajit_nfile_importer.hh
    common/luajit_queue.hh
    common/luajit_ref.hh
    common/luajit_thread.hh
    common/luajit_thread.cc
    common/luajit_value.cc
    common/memento.hh
    common/memento_recorder.hh
    common/mutex.hh
    common/nfile_watcher.hh
    common/node.h
    common/node.hh
    common/node_link_store.hh
    common/node_root_lambda.hh
    common/ptr_selector.hh
    common/pure_node_file.hh
    common/queue.hh
    common/ring_buffer.hh
    common/squashed_history.hh
    common/stopwatch.hh
    common/task.hh
    common/thread.hh
    common/timed_queue.hh
    common/util_algorithm.hh
    common/value.hh
    common/yaml_nf7.hh
    common/yas_enum.hh
    common/yas_imgui.hh
    common/yas_imnodes.hh
    common/yas_nf7.hh
    common/yas_std_atomic.hh
    common/yas_std_filesystem.hh
    common/yas_std_variant.hh

    file/audio_context.cc
    file/audio_device.cc
    file/font_context.cc
    file/font_face.cc
    file/gl_obj.cc
    file/luajit_context.cc
    file/luajit_node.cc
    file/node_comment.cc
    file/node_dll.cc
    file/node_exprtk.cc
    file/node_mutex.cc
    file/node_network.cc
    file/node_ref.cc
    file/node_singleton.cc
    file/node_ziptie.cc
    file/system_dir.cc
    file/system_event.cc
    file/system_imgui.cc
    file/system_logger.cc
    file/system_node.cc
    file/value_curve.cc
    file/value_imm.cc
    file/value_plot.cc
)
target_link_libraries(nf7
  PRIVATE
    exprtk
    freetype
    glew
    glfw
    imgui
    imnodes
    implot
    luajit
    magic_enum
    miniaudio
    source_location
    TracyClient
    yas
    yaml-cpp
)
