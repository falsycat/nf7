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

  file(RELATIVE_PATH CURRENT_DIR "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
  set(GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
  set(GENERATED_CURRENT_DIR "${GENERATED_DIR}/${CURRENT_DIR}")

  if (IS_ABSOLUTE "${args_src}")
    set(src_abs "${args_src}")
  else()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${args_src}")
      set(src_abs "${CMAKE_CURRENT_SOURCE_DIR}/${args_src}")
    elseif(EXISTS "${GENERATED_CURRENT_DIR}/${args_src}")
      set(src_abs "${GENERATED_CURRENT_DIR}/${args_src}")
    else()
      message(FATAL_ERROR "no source file found: ${args_src}")
    endif()
  endif()

  get_filename_component(src_ext  "${src_abs}" LAST_EXT)
  get_filename_component(src_name "${src_abs}" NAME)
  string(REGEX REPLACE "${src_ext}$" "" dst_name "${src_name}")

  file(MAKE_DIRECTORY "${GENERATED_CURRENT_DIR}")
  set(dst_abs "${GENERATED_CURRENT_DIR}/${dst_name}")

  add_custom_command(
    COMMAND "${src_abs}" ${args_ARGS} > "${dst_abs}"
    OUTPUT "${dst_abs}"
    DEPENDS "${src_abs}" ${args_DEPENDS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
  )
  target_sources(${args_target} ${args_scope} "${dst_abs}")
endfunction()
