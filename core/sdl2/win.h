// No copyright
#pragma once

#include <stdint.h>

#include <SDL.h>

#include "nf7.h"

#include "util/malloc.h"

#include "core/sdl2/mod.h"


struct nf7_core_sdl2_win {
  struct nf7_core_sdl2*   mod;
  struct nf7_util_malloc* malloc;

  SDL_Window* win;
  uint32_t    win_id;
  void*       gl;

  struct nf7_util_signal_recv event_recv;
};

bool nf7_core_sdl2_win_init(struct nf7_core_sdl2_win*);
void nf7_core_sdl2_win_deinit(struct nf7_core_sdl2_win*);
