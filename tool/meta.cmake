# No copyright

# This function adds a custom command to execute ${args_src} and adds its output
# to sources of ${args_target} with a scope ${args_scope}
function(target_meta_source args_target args_scope args_src)
  cmake_parse_arguments(args
    ""
    ""
    "ARGS;DEPENDS"
    ${ARGN}
  )

  if (IS_ABSOLUTE "${args_src}")
    set(src_abs "${args_src}")
  else()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${args_src}")
      set(src_abs "${CMAKE_CURRENT_SOURCE_DIR}/${args_src}")
    elseif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${args_src}")
      set(src_abs "${CMAKE_CURRENT_BINARY_DIR}/${args_src}")
    else()
      message(FATAL_ERROR "no source file found: ${args_src}")
    endif()
  endif()

  get_filename_component(src_ext  "${src_abs}" LAST_EXT)
  get_filename_component(src_name "${src_abs}" NAME)
  string(REGEX REPLACE "${src_ext}$" "" dst_name "${src_name}")

  get_filename_component(src_dir_abs "${src_abs}" DIRECTORY)
  string(REGEX REPLACE "^${CMAKE_SOURCE_DIR}/" "" src_dir "${src_dir_abs}")

  set(dst_dir_abs "${CMAKE_BINARY_DIR}/generated/${src_dir}")
  file(MAKE_DIRECTORY "${dst_dir_abs}")
  set(dst_abs "${dst_dir_abs}/${dst_name}")

  add_custom_command(
    COMMAND "${src_abs}" ${args_ARGS} > "${dst_abs}"
    OUTPUT "${dst_abs}"
    DEPENDS ${args_DEPENDS}
    WORKING_DIRECTORY "${src_dir_abs}"
    VERBATIM
  )
  target_sources(${args_target} ${args_scope} "${dst_abs}")
endfunction()
