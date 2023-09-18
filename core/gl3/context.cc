// No copyright
#include "core/gl3/context.hh"

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

#include "iface/common/exception.hh"
#include "iface/subsys/parallelism.hh"

#include "core/logger.hh"


using namespace std::literals;

namespace nf7::core::gl3 {

Context::Context(Env& env, const std::shared_ptr<Impl>& impl)
try : subsys::Interface("nf7::core::gl3::Context"),
      impl_(impl? impl: std::make_shared<Impl>(env)) {
  impl_->Main();
} catch (const std::bad_alloc&) {
  throw MemoryException {};
}
Context::~Context() noexcept {
  impl_->Exit();
}
void Context::Push(Task&& task) noexcept {
  impl_->Push(std::move(task));
}


class Context::Impl::TaskDriver final {
 public:
  TaskDriver() = delete;
  explicit TaskDriver(const std::shared_ptr<subsys::Logger>& logger,
                      Task::Time time) noexcept
      : logger_(logger), time_(time) { }

 public:
  void BeginBusy() noexcept { }
  void EndBusy() noexcept { }

  void Drive(Task&& task) noexcept
  try {
    task(ctx_);
  } catch (Exception& e) {
    logger_->Warn("GL3 task caused an exception");
  }

 public:
  Task::Time tick() const noexcept { return time_; }
  bool nextIdleInterruption() const noexcept { return true; }
  bool nextTaskInterruption() const noexcept { return false; }

 private:
  const std::shared_ptr<subsys::Logger> logger_;
  const Task::Time time_;

  TaskContext ctx_ {};
};

Context::Impl::Impl(Env& env)
try : clock_(env.Get<subsys::Clock>()),
      concurrency_(env.Get<subsys::Concurrency>()),
      logger_(env.GetOr<subsys::Logger>(NullLogger::kInstance)) {
  sdl_ = 0 == SDL_Init(SDL_INIT_VIDEO);
  if (!sdl_) {
    throw Exception {"SDL init failure"};
  }
  SetUpGL();
  SetUpWindow();
} catch (const Exception&) {
  TearDown();
  throw;
}

void Context::Impl::Main() noexcept {
  const bool alive = Update();
  const auto now   = clock_->now();

  TaskDriver driver {logger_, now};
  tasq_.Drive(driver);

  if (alive_ && alive) {
    concurrency_->Push(nf7::SyncTask {
      now + 33ms,
      [wself = weak_from_this()](auto&) {
        if (auto self = wself.lock()) { self->Main(); }
      },
    });
  } else {
    TearDown();
  }
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
  SDL_GL_SetSwapInterval(0);
}

void Context::Impl::TearDown() noexcept {
  if (nullptr != gl_) {
    SDL_GL_DeleteContext(gl_);
  }
  if (nullptr != win_) {
    SDL_DestroyWindow(win_);
  }
  if (sdl_) {
    SDL_Quit();
  }
}

}  // namespace nf7::core::gl3
