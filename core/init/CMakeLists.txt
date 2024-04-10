add_library(nf7core_init)
target_sources(nf7core_init
  PRIVATE
    mod.c
    factory.priv.h
)
target_link_libraries(nf7core_init
  PRIVATE
    nf7if
    nf7util
)

