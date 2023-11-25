// No copyright
#pragma once

#include <stdint.h>

#include <SDL.h>
#include <uv.h>

#include "nf7.h"

#include "util/malloc.h"
#include "util/signal.h"


extern const struct nf7_mod_meta nf7_core_sdl2;

struct nf7_core_sdl2 {
  const struct nf7_mod_meta* meta;

  // library pointers (immutable)
  const struct nf7*       nf7;
  struct nf7_util_malloc* malloc;
  uv_loop_t*              uv;
  SDL_Window*             win;
  void*                   gl;

  // uv handles (immutable)
  uv_timer_t poll_timer;

  // signals
  struct nf7_util_signal update;

  // mutable parameters
  uint64_t poll_interval;
};
