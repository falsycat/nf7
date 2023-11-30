// No copyright
#include "core/sdl2/win.h"

#include <assert.h>

#include <SDL.h>

#include "util/log.h"


static void setup_gl_(void);
static void handle_(struct nf7_util_signal_recv* recv);


bool nf7_core_sdl2_win_init(struct nf7_core_sdl2_win* this) {
  assert(nullptr != this);
  assert(nullptr != this->mod);
  assert(nullptr != this->malloc);

  // TODO error handling

  this->event_recv = (struct nf7_util_signal_recv) {
    .signal = &this->mod->event_signal,
  };
  this->event_recv.data = this;
  this->event_recv.func = handle_;
  if (!nf7_util_signal_recv_set(&this->event_recv, &this->mod->event_signal)) {
    nf7_util_log_error("failed to listen event signal");
    return false;
  }

  setup_gl_();
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  this->win = SDL_CreateWindow(
      "Nf7",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      1280, 720,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (nullptr == this->win) {
    nf7_util_log_error("failed to create SDL window: %s", SDL_GetError());
    return false;
  }
  this->win_id = SDL_GetWindowID(this->win);
  if (0 == this->win_id) {
    nf7_util_log_error("failed to get window id: %s", SDL_GetError());
    return false;
  }
  nf7_util_log_debug("GUI window %" PRIu32 " is created", this->win_id);

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

  nf7_core_sdl2_ref(this->mod);
  return true;
}

void nf7_core_sdl2_win_deinit(struct nf7_core_sdl2_win* this) {
  if (nullptr != this) {
    if (nullptr != this->gl) {
      SDL_GL_DeleteContext(this->gl);
      nf7_util_log_debug("OpenGL context is deleted");
    }
    if (nullptr != this->win) {
      SDL_DestroyWindow(this->win);
      nf7_util_log_debug("GUI window is destroyed");
    }
    nf7_util_signal_recv_unset(&this->event_recv);
    nf7_core_sdl2_unref(this->mod);
  }
}

static void setup_gl_(void) {
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
}

static void handle_(struct nf7_util_signal_recv* recv) {
  struct nf7_core_sdl2_win* this = recv->data;
  assert(nullptr != this);

  const SDL_Event* e = this->mod->event;
  if (SDL_WINDOWEVENT != e->type) {
    return;
  }

  const SDL_WindowEvent* we = &e->window;
  if (this->win_id != we->windowID) {
    return;
  }

  switch (we->event) {
  case SDL_WINDOWEVENT_CLOSE:
    nf7_util_log_debug("received close request");
    break;
  default:
    break;
  }

  // Be careful, the handler may delete `this`.
  if (nullptr != this->handler) {
    this->handler(this, we);
  }
}
