add_library(nf7util)
target_sources(nf7util
  PRIVATE
    malloc.c
  PUBLIC
    array.h
    log.h
    malloc.h
    signal.h
)
target_link_libraries(nf7util PRIVATE nf7config)
