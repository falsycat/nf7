// No copyright
#pragma once

#include <assert.h>

#include "util/log.h"

#include "core/sdl2/mod.h"


struct nf7_core_sdl2_poll {
  const struct nf7*       nf7;
  struct nf7_util_malloc* malloc;
  uv_loop_t*              uv;

  uv_timer_t timer;
  uint64_t   interval;

  void* data;
  void (*handler)(struct nf7_core_sdl2_poll*, const SDL_Event*);
};


static struct nf7_core_sdl2_poll* poll_new_(struct nf7_core_sdl2*);
static void poll_del_(struct nf7_core_sdl2_poll*);

static void poll_on_time_(uv_timer_t*);
static void poll_on_close_(uv_handle_t*);


static struct nf7_core_sdl2_poll* poll_new_(struct nf7_core_sdl2* mod) {
  assert(nullptr != mod);

  struct nf7_core_sdl2_poll* this =
      nf7_util_malloc_new(mod->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7_util_log_error("failed to allocate poll context");
    return nullptr;
  }
  *this = (struct nf7_core_sdl2_poll) {
    .nf7    = mod->nf7,
    .malloc = mod->malloc,
    .uv     = mod->uv,

    .interval = 30,
  };

  if (0 != nf7_util_log_uv(uv_timer_init(this->uv, &this->timer))) {
    nf7_util_log_error("failed to init poll timer");
    return nullptr;
  }
  uv_unref((uv_handle_t*) &this->timer);
  this->timer.data = this;

  if (0 != nf7_util_log_uv(uv_timer_start(&this->timer, poll_on_time_, 0, 0))) {
    nf7_util_log_error("failed to start poll timer");
    poll_del_(this);
    return nullptr;
  }
  return this;
}
static void poll_del_(struct nf7_core_sdl2_poll* this) {
  if (this == nullptr) {
    return;
  }
  if (this == this->timer.data) {
    this->handler = nullptr;
    uv_close((uv_handle_t*) &this->timer, poll_on_close_);
    return;
  }
  nf7_util_malloc_del(this->malloc, this);
}

static void poll_on_time_(uv_timer_t* timer) {
  assert(nullptr != timer);

  struct nf7_core_sdl2_poll* this = timer->data;

  SDL_Event e;
  while (0 != SDL_PollEvent(&e)) {
    if (nullptr != this->handler) {
      this->handler(this, &e);
    }
  }

  if (0 != uv_timer_start(&this->timer, poll_on_time_, this->interval, 0)) {
    nf7_util_log_error("failed to restart poll timer");
    poll_del_(this);
  }
}

static void poll_on_close_(uv_handle_t* handle) {
  struct nf7_core_sdl2_poll* this = handle->data;
  assert(nullptr != this);
  handle->data = nullptr;
  poll_del_(this);
}
