// No copyright
#include "core/sdl2/mod.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>

#include <SDL.h>

#include "nf7.h"

#include "util/log.h"
#include "util/malloc.h"

#include "core/sdl2/poll.h"


static atomic_uint_least32_t sdl_refcnt_ = 0;


static void finalize_(struct nf7_core_sdl2*);
NF7_UTIL_REFCNT_IMPL(, nf7_core_sdl2, {finalize_(this);});


struct nf7_mod* nf7_core_sdl2_new(const struct nf7* nf7) {
  assert(nullptr != nf7);

  if (0 < atomic_fetch_add(&sdl_refcnt_, 1)) {
    nf7_util_log_error(
        "multiple SDL2 module instance cannot be exists at the same time");
    return nullptr;
  }
  if (0 != SDL_Init(SDL_INIT_VIDEO)) {
    nf7_util_log_error("failed to init SDL: %s", SDL_GetError());
    return nullptr;
  }

  struct nf7_core_sdl2* this = nf7_util_malloc_new(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7_util_log_error("failed to allocate instance");
    goto ABORT;
  }
  *this = (struct nf7_core_sdl2) {
    .meta   = &nf7_core_sdl2,
    .nf7    = nf7,
    .malloc = nf7->malloc,
    .uv     = nf7->uv,
  };

  this->poll = poll_new_(this);
  if (nullptr == this->poll) {
    nf7_util_log_error("failed to setup polling");
    goto ABORT;
  }

  nf7_core_sdl2_ref(this);
  return (struct nf7_mod*) this;

ABORT:
  nf7_util_log_warn("initialization is aborted");
  finalize_(this);
  return nullptr;
}

static void finalize_(struct nf7_core_sdl2* this) {
  if (nullptr == this) {
    return;
  }

  poll_del_(this->poll);
  nf7_util_malloc_del(this->malloc, this);

  if (1 == atomic_fetch_sub(&sdl_refcnt_, 1)) {
    nf7_util_log_debug("finalizing SDL...");
    SDL_Quit();
    nf7_util_log_info("SDL is finalized");
  }
}

static void unref_(struct nf7_mod* this_) {
  struct nf7_core_sdl2* this = (struct nf7_core_sdl2*) this_;
  nf7_core_sdl2_unref(this);
}

const struct nf7_mod_meta nf7_core_sdl2 = {
  .name = (const uint8_t*) "nf7core_sdl2",
  .desc = (const uint8_t*) "provides SDL2 features",
  .ver  = 0,

  .delete = unref_,
};
