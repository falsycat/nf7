function(_imgui4lua_main)
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(py  "${PROJECT_SOURCE_DIR}/cmake/imgui4lua.py")
  set(src "${imgui_SOURCE_DIR}/imgui.h")
  set(dst "${dir}/imgui4lua.inc")

  find_program(SH      sh      REQUIRED)
  find_program(PYTHON3 python3 REQUIRED)
  find_program(CLANGXX clang++ REQUIRED)

  make_directory("${dir}")
  add_custom_command(
    COMMAND  ${SH} -c "${PYTHON3} '${py}' '${src}' > '${dst}'"
    OUTPUT  "${dst}"
    DEPENDS "${src}" "${py}"
    VERBATIM
  )

  add_library(imgui4lua INTERFACE)
  target_sources(imgui4lua PUBLIC "${dst}")
  target_include_directories(imgui4lua INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
_imgui4lua_main()
