# ---- basic modules
set(MODS
  sdl2
  test
)

# ---- core library
add_library(nf7core)
target_link_libraries(nf7core PRIVATE nf7if)

foreach(name IN LISTS MODS)
  add_subdirectory(${name})
  target_link_libraries(nf7core PRIVATE nf7core_${name})
endforeach()

# ---- generate all.h
target_meta_source(nf7core
  PRIVATE all.c.sh
  ARGS    ${MODS}
)
