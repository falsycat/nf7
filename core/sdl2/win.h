// No copyright
#pragma once

#include <stdint.h>

#include <SDL.h>

#include "nf7.h"

#include "util/malloc.h"

#include "core/sdl2/mod.h"


struct nf7core_sdl2_win {
  struct nf7core_sdl2*   mod;
  struct nf7util_malloc* malloc;

  SDL_Window* win;
  uint32_t    win_id;
  void*       gl;

  struct nf7util_signal_recv event_recv;

  void* data;
  void (*handler)(struct nf7core_sdl2_win*, const SDL_WindowEvent*);
};

bool nf7core_sdl2_win_init(struct nf7core_sdl2_win*, struct nf7core_sdl2*);
void nf7core_sdl2_win_deinit(struct nf7core_sdl2_win*);
