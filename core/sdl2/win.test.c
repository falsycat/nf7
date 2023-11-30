// No copyright
#include "core/sdl2/win.h"

#include <uv.h>

#include "test/common.h"

#include "util/log.h"

#include "core/sdl2/mod.h"


struct nf7_core_sdl2_win_test {
  struct nf7_test* test;
  struct nf7_util_malloc* malloc;
  uv_loop_t* uv;

  struct nf7_core_sdl2_win win;
  uv_timer_t timer;
};

static void finalize_(struct nf7_core_sdl2_win_test*);
static void on_time_(uv_timer_t*);
static void on_close_(uv_handle_t*);

NF7_TEST(nf7_core_sdl2_win_test) {
  struct nf7_core_sdl2* mod = (void*) nf7_get_mod_by_meta(
      test_->nf7, (struct nf7_mod_meta*) &nf7_core_sdl2);
  if (!nf7_test_expect(nullptr != mod)) {
    return false;
  }

  struct nf7_core_sdl2_win_test* this =
    nf7_util_malloc_new(test_->malloc, sizeof(*this));
  if (!nf7_test_expect(nullptr != this)) {
    goto ABORT;
  }
  *this = (struct nf7_core_sdl2_win_test) {
    .test   = test_,
    .malloc = test_->malloc,
    .uv     = test_->nf7->uv,
    .win = {
      .mod    = mod,
      .malloc = test_->malloc,
    },
  };
  nf7_test_ref(this->test);

  if (!nf7_test_expect(nf7_core_sdl2_win_init(&this->win))) {
    goto ABORT;
  }

  if (0 != nf7_util_log_uv(uv_timer_init(this->uv, &this->timer))) {
    goto ABORT;
  }
  this->timer.data = this;

  if (0 != nf7_util_log_uv(uv_timer_start(&this->timer, on_time_, 3000, 0))) {
    goto ABORT;
  }
  return true;

ABORT:
  finalize_(this);
  return false;
}

static void finalize_(struct nf7_core_sdl2_win_test* this) {
  if (nullptr != this->timer.data) {
    uv_close((uv_handle_t*) &this->timer, on_close_);
    return;
  }
  nf7_core_sdl2_win_deinit(&this->win);
  nf7_test_unref(this->test);
  nf7_util_malloc_del(this->malloc, this);
}

static void on_time_(uv_timer_t* timer) {
  struct nf7_core_sdl2_win_test* this = timer->data;
  finalize_(this);
}

static void on_close_(uv_handle_t* handle) {
  struct nf7_core_sdl2_win_test* this = handle->data;
  handle->data = nullptr;
  finalize_(this);
}
