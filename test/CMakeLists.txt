# ---- test interfae library
add_library(nf7test_if INTERFACE)
target_link_libraries(nf7test_if INTERFACE nf7config)
target_sources(nf7test_if INTERFACE common.h)

# ---- test library
add_library(nf7test)
target_link_libraries(nf7test
  PUBLIC
    nf7config
    nf7test_if
)
target_meta_source(nf7test
  PRIVATE run.c.sh
  ARGS    $<TARGET_PROPERTY:nf7test_src,SOURCES>
  DEPENDS $<TARGET_PROPERTY:nf7test_src,SOURCES>
)

# ---- a custom target to keep source files of tests
add_custom_target(nf7test_src)

function(target_tests target)
  if (NOT TARGET "${target}")
    message(FATAL_ERROR "unknown target: ${target}")
  endif()

  target_sources(${target} PRIVATE ${ARGN})
  target_link_libraries(${target} PRIVATE nf7test_if)

  target_sources(nf7test_src PRIVATE ${ARGN})
  target_link_libraries(nf7test PRIVATE ${target})
endfunction()
