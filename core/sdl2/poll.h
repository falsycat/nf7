// No copyright
#pragma once

#include <assert.h>

#include "util/log.h"

#include "core/sdl2/mod.h"


struct nf7_core_sdl2_poll {
  struct nf7_core_sdl2* mod;

  const struct nf7*       nf7;
  struct nf7_util_malloc* malloc;
  uv_loop_t*              uv;

  uv_timer_t timer;

  uint64_t interval;
};


static bool poll_setup_(struct nf7_core_sdl2*);
static void poll_cancel_(struct nf7_core_sdl2_poll*);

static void poll_proc_(uv_timer_t*);
static void poll_finalize_(uv_handle_t*);


static bool poll_setup_(struct nf7_core_sdl2* mod) {
  assert(nullptr != mod);

  struct nf7_core_sdl2_poll* this =
      nf7_util_malloc_new(mod->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7_util_log_error("failed to allocate poll context");
    return false;
  }
  *this = (struct nf7_core_sdl2_poll) {
    .mod    = mod,
    .nf7    = mod->nf7,
    .malloc = mod->malloc,
    .uv     = mod->uv,

    .interval = 30,
  };

  if (0 != uv_timer_init(this->uv, &this->timer)) {
    nf7_util_log_error("failed to init poll timer");
    return false;
  }
  this->timer.data = this;

  if (0 != uv_timer_start(&this->timer, poll_proc_, 0, 0)) {
    nf7_util_log_error("failed to start poll timer");
    poll_cancel_(this);
    return false;
  }
  return true;
}
static void poll_cancel_(struct nf7_core_sdl2_poll* this) {
  assert(nullptr != this);
  if (this == this->timer.data) {
    uv_close((uv_handle_t*) &this->timer, poll_finalize_);
  }
}

static void poll_proc_(uv_timer_t* timer) {
  assert(nullptr != timer);

  struct nf7_core_sdl2_poll* this = timer->data;

  SDL_Event e;
  while (0 != SDL_PollEvent(&e)) {
    if (SDL_QUIT == e.type) {
      nf7_util_log_info("SDL2 event poller is uninstalled");
      poll_cancel_(this);
      return;
    }
  }

  if (0 != uv_timer_start(&this->timer, poll_proc_, this->interval, 0)) {
    nf7_util_log_error("failed to restart poll timer");
    poll_cancel_(this);
  }
}

static void poll_finalize_(uv_handle_t* handle) {
  struct nf7_core_sdl2_poll* this = handle->data;
  assert(nullptr != this);

  nf7_util_malloc_del(this->malloc, this);
}
