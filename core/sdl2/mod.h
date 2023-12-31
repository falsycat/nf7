// No copyright
#pragma once

#include <stdint.h>

#include <SDL.h>
#include <uv.h>

#include "nf7.h"

#include "util/malloc.h"
#include "util/refcnt.h"
#include "util/signal.h"


extern const struct nf7_mod_meta nf7core_sdl2;

struct nf7core_sdl2_poll;

struct nf7core_sdl2 {
  struct nf7_mod super;

  // library pointers (immutable)
  const struct nf7*      nf7;
  struct nf7util_malloc* malloc;
  uv_loop_t*             uv;
  SDL_Window*            win;
  void*                  gl;

  uint32_t refcnt;

  struct nf7core_sdl2_poll* poll;
  const SDL_Event*       event;
  struct nf7util_signal  event_signal;
};
NF7UTIL_REFCNT_DECL(, nf7core_sdl2);
