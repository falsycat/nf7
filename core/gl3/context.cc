// No copyright
#include "core/gl3/context.hh"

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

#include "iface/common/exception.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/logger.hh"
#include "iface/subsys/parallelism.hh"

#include "core/logger.hh"


namespace nf7::core::gl3 {

class Context::Impl final : public std::enable_shared_from_this<Impl> {
 public:
  explicit Impl(Env& env)
  try : concurrency_(env.GetOr<subsys::Concurrency>()),
        logger_(env.GetOr<subsys::Logger>(NullLogger::kInstance)) {
    sdl_video_ = !SDL_Init(SDL_INIT_VIDEO);
    if (!sdl_video_) {
      throw Exception {"SDL init failure"};
    }
    SetUpGL();
    SetUpWindow();
  } catch (const Exception&) {
    TearDown();
    throw;
  }

 public:
  void Main() noexcept {
    SDL_GL_MakeCurrent(win_, gl_);
    SDL_GL_SetSwapInterval(1);

    while (alive_) try {
      SDL_GL_SwapWindow(win_);
    } catch (Exception& e) {
      logger_->Warn("error while GL3 window main loop");
    }
    concurrency_->Exec(
        [this, self = shared_from_this()](auto&) { TearDown(); });
  }
  void Exit() noexcept {
    alive_ = false;
  }

 private:
  void SetUpGL() noexcept;
  void SetUpWindow();

  void TearDown() noexcept {
    if (nullptr != gl_) {
      SDL_GL_DeleteContext(gl_);
    }
    if (nullptr != win_) {
      SDL_DestroyWindow(win_);
    }
    if (sdl_video_) {
      SDL_Quit();
    }
  }

 private:
  const std::shared_ptr<subsys::Concurrency> concurrency_;
  const std::shared_ptr<subsys::Logger> logger_;

  std::atomic<bool> alive_ {true};

  bool sdl_video_ {false};
  SDL_Window* win_ {nullptr};
  void* gl_ {nullptr};
};

Context::Context(Env& env)
    : subsys::Interface("nf7::core::gl3::Context"),
      impl_(std::make_shared<Impl>(env)) {
  env.Get<subsys::Parallelism>()->Exec([impl = impl_](auto&) { impl->Main(); });
}
Context::~Context() noexcept {
  impl_->Exit();
}

void Context::Impl::SetUpGL() noexcept {
# if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
# else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
# endif
}

void Context::Impl::SetUpWindow() {
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  constexpr auto flags {
      SDL_WINDOW_OPENGL
      | SDL_WINDOW_RESIZABLE
      | SDL_WINDOW_ALLOW_HIGHDPI
  };
  win_ = SDL_CreateWindow(
      "Dear ImGui SDL2+OpenGL3 example",
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      1280, 720,
      flags);
  if (nullptr == win_) {
    throw Exception {"failed to create new window"};
  }

  gl_ = SDL_GL_CreateContext(win_);
  if (nullptr == gl_) {
    throw Exception {"failed to create new GL context"};
  }
}

}  // namespace nf7::core::gl3
