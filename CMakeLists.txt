cmake_minimum_required(VERSION 3.18)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# ---- configuration ----
project(nf7 C CXX)

option(NF7_STATIC "link all libs statically" ON)

set(NF7_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include/generated")
file(MAKE_DIRECTORY "${NF7_GENERATED_INCLUDE_DIR}")

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(NF7_CXX_FLAGS
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
    -Wall -Werror -pedantic-errors -Wextra -Wconversion -Wsign-conversion>
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
    -Wno-overloaded-virtual>
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4 /WX /Zc:__cplusplus /external:anglebrackets /external:W0>
)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_subdirectory(thirdparty EXCLUDE_FROM_ALL)
add_subdirectory(tool)


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

    common/aggregate_command.hh
    common/async_buffer.hh
    common/async_buffer_adaptor.hh
    common/audio_queue.hh
    common/buffer.hh
    common/dir.hh
    common/dir_item.hh
    common/file_base.hh
    common/file_holder.hh
    common/file_holder.cc
    common/future.hh
    common/generic_context.hh
    common/generic_history.hh
    common/generic_memento.hh
    common/generic_type_info.hh
    common/generic_watcher.hh
    common/gui_dnd.hh
    common/gui_context.hh
    common/gui_file.hh
    common/gui_file.cc
    common/gui_node.hh
    common/gui_popup.hh
    common/gui_popup.cc
    common/gui_timeline.hh
    common/gui_timeline.cc
    common/gui_value.hh
    common/gui_value.cc
    common/gui_window.hh
    common/history.hh
    common/life.hh
    common/lock.hh
    common/logger.hh
    common/logger_ref.hh
    common/luajit.hh
    common/luajit.cc
    common/luajit_obj.hh
    common/luajit_queue.hh
    common/luajit_ref.hh
    common/luajit_thread.hh
    common/luajit_thread.cc
    common/luajit_thread_lambda.hh
    common/memento.hh
    common/memento_recorder.hh
    common/native_file.hh
    common/node.hh
    common/node_link_store.hh
    common/ptr_selector.hh
    common/queue.hh
    common/sequencer.hh
    common/squashed_history.hh
    common/task.hh
    common/thread.hh
    common/timed_queue.hh
    common/util_string.hh
    common/value.hh
    common/yas_audio.hh
    common/yas_imgui.hh
    common/yas_imnodes.hh
    common/yas_nf7.hh
    common/yas_std_atomic.hh
    common/yas_std_filesystem.hh
    common/yas_std_variant.hh

    $<$<PLATFORM_ID:Linux>:common/native_file_unix.cc>
    $<$<PLATFORM_ID:Windows>:common/native_file_win.cc>

    file/audio_context.cc
    file/audio_device.cc
    file/luajit_context.cc
    file/luajit_inline_node.cc
    file/luajit_node.cc
    file/luajit_obj.cc
    file/node_imm.cc
    file/node_network.cc
    file/node_ref.cc
    file/sequencer_adaptor.cc
    file/sequencer_call.cc
    file/sequencer_timeline.cc
    file/system_dir.cc
    file/system_imgui.cc
    file/system_logger.cc
    file/system_native_file.cc
    file/value_curve.cc
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
    miniaudio
    source_location
    yas
)

# ---- resource compile ----
set(NF7_DEFAULT_ROOT_INC "${NF7_GENERATED_INCLUDE_DIR}/root.nf7.inc")
add_custom_command(
  OUTPUT ${NF7_DEFAULT_ROOT_INC}
  DEPENDS nf7-init
  COMMAND $<TARGET_FILE:nf7-init> > ${NF7_DEFAULT_ROOT_INC}
  COMMENT "generating root.nf7..."
  VERBATIM
)
target_sources(nf7 PRIVATE ${NF7_DEFAULT_ROOT_INC})
