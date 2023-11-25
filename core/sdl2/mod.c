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


static bool new_setup_gl_(void);
static bool new_init_win_(struct nf7_core_sdl2*);

static void del_(struct nf7_mod*);


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

    .poll_interval = 30,
  };
  nf7_util_signal_init(&this->update, this->malloc);

  if (!(new_setup_gl_() && new_init_win_(this))) {
    nf7_util_log_error("failed to setup OpenGL");
    goto ABORT;
  }
  nf7_util_log_debug("OpenGL is ready");

  if (!poll_init_(this)) {
    nf7_util_log_error("failed to setup event polling");
    goto ABORT;
  }
  nf7_util_log_debug("polling is activated");

  return (struct nf7_mod*) this;

ABORT:
  nf7_util_log_warn("initialization is aborted");
  del_((struct nf7_mod*) this);
  return nullptr;
}
static bool new_setup_gl_(void) {
# if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
# else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
# endif
  return true;
}
static bool new_init_win_(struct nf7_core_sdl2* this) {
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  this->win = SDL_CreateWindow(
      "helloworld",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      1280, 720,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (nullptr == this->win) {
    nf7_util_log_error("failed to create SDL window: %s", SDL_GetError());
    return false;
  }
  nf7_util_log_debug("GUI window is created");

  this->gl = SDL_GL_CreateContext(this->win);
  if (nullptr == this->gl) {
    nf7_util_log_error("failed to create GL context: %s", SDL_GetError());
    return false;
  }
  nf7_util_log_debug("OpenGL context is created");

  if (0 != SDL_GL_SetSwapInterval(0)) {
    nf7_util_log_warn(
        "failed to set swap interval, this will cause a performance issue: %s",
        SDL_GetError());
  }
  return true;
}


static void del_(struct nf7_mod* this_) {
  struct nf7_core_sdl2* this = (struct nf7_core_sdl2*) this_;
  if (nullptr != this) {
    if (nullptr != this->gl) {
      SDL_GL_DeleteContext(this->gl);
      nf7_util_log_debug("OpenGL context is deleted");
    }
    if (nullptr != this->win) {
      SDL_DestroyWindow(this->win);
      nf7_util_log_debug("GUI window is destroyed");
    }
    nf7_util_signal_deinit(&this->update);
    nf7_util_malloc_del(this->malloc, this);
  }
  if (1 == atomic_fetch_sub(&sdl_refcnt_, 1)) {
    nf7_util_log_debug("finalizing SDL...");
    SDL_Quit();
    nf7_util_log_info("SDL is finalized");
  }
}


const struct nf7_mod_meta nf7_core_sdl2 = {
  .name = (const uint8_t*) "nf7core_sdl2",
  .desc = (const uint8_t*) "provides SDL2 features",
  .ver  = 0,

  .delete = del_,
};
