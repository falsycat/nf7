// No copyright
#pragma once

#include <assert.h>

#include "core/sdl2/mod.h"

static bool poll_init_(struct nf7_core_sdl2*);
static void poll_deinit_(struct nf7_core_sdl2*);

static void poll_proc_(uv_timer_t*);


static bool poll_init_(struct nf7_core_sdl2* this) {
  assert(nullptr != this);

  if (0 != uv_timer_init(this->uv, &this->poll_timer)) {
    return false;
  }
  this->poll_timer.data = this;

  if (0 != uv_timer_start(&this->poll_timer, poll_proc_, 0, 0)) {
    poll_deinit_(this);
    return false;
  }
  return true;
}
static void poll_deinit_(struct nf7_core_sdl2* this) {
  assert(nullptr != this);
  uv_close((uv_handle_t*) &this->poll_timer, nullptr);
}

static void poll_proc_(uv_timer_t* timer) {
  assert(nullptr != timer);

  struct nf7_core_sdl2* this = timer->data;

  SDL_Event e;
  while (0 != SDL_PollEvent(&e)) { }

  if (0 != uv_timer_start(&this->poll_timer, poll_proc_, this->poll_interval, 0)) {
    poll_deinit_(this);
  }
}
