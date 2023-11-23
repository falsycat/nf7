// No copyright
#pragma once

#include <SDL.h>
#include <uv.h>

#include "nf7.h"

#include "util/malloc.h"


extern const struct nf7_mod_meta nf7_core_sdl2;

struct nf7_core_sdl2 {
  const struct nf7_mod_meta* meta;

  const struct nf7*       nf7;
  struct nf7_util_malloc* malloc;
  uv_loop_t*              uv;
  SDL_Window*             win;
};
