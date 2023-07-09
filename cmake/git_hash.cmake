set(dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(out "${dir}/git_hash.hh")

find_program(SH sh REQUIRED)

make_directory("${dir}")
add_custom_command(
  COMMAND  ${SH} -c "echo constexpr const char\* GIT_HASH = \\\"`git rev-parse --short HEAD``git diff --quiet && echo \\\*`\\\"\\; > ${out}"
  OUTPUT  "${out}"
  DEPENDS "${PROJECT_SOURCE_DIR}/.git/HEAD"
  VERBATIM
)

add_library(git_hash INTERFACE)
target_sources(git_hash PUBLIC "${out}")
target_include_directories(git_hash INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

unset(out)
unset(dir)
