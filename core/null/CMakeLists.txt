add_library(nf7core_null)
target_sources(nf7core_null
  PRIVATE
    idea.c
    mod.c
  PUBLIC
    mod.h
)
target_link_libraries(nf7core_null
  PRIVATE
    nf7if
    nf7util
)

