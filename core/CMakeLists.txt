# ---- basic modules
set(MODS
  # ---- interface layer
  exec

  # ---- implementation layer
  lua
  null
  sdl2

  # ---- usecase layer
  init
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
