add_library(nf7core_any)
target_sources(nf7core_any
  PRIVATE
    idea.c
    mod.c
)
target_link_libraries(nf7core_any
  PRIVATE
    nf7if
    nf7util

  PUBLIC
    nf7core_exec
)

