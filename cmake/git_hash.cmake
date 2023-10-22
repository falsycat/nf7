function(_git_hash_main)
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(out "${dir}/git_hash.hh")

  find_program(BASH bash REQUIRED)
  find_program(GIT  git  REQUIRED)
  
  make_directory("${dir}")
  add_custom_target(git_hash_generate
    COMMAND ${BASH} -c "echo constexpr const char\* GIT_HASH = \\\"$(${GIT} rev-parse --short HEAD)$([[ -z $(${GIT} status -s) ]] || echo \\\*)\\\"\\; > ${out}.temp"
    COMMAND ${BASH} -c "diff '${out}.temp' '${out}' &> /dev/null || mv '${out}.temp' '${out}'"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
  )
  
  add_library(git_hash INTERFACE)
  add_dependencies(git_hash git_hash_generate)
  target_sources(git_hash PUBLIC "${out}")
  target_include_directories(git_hash INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
_git_hash_main()
