// No copyright
#include "core/gl3/context.hh"

#include "iface/common/exception.hh"
#include "iface/subsys/clock.hh"
#include "iface/subsys/logger.hh"
#include "iface/subsys/parallelism.hh"

#include "core/logger.hh"


using namespace std::literals;

namespace nf7::core::gl3 {

class Context::Impl : public std::enable_shared_from_this<Impl> {
 private:
  static constexpr const auto kPollingInterval = 17ms;

 public:
  Impl(Env& env, Context& ctx)
  try : clock_(env.Get<subsys::Clock>()),
        concurrency_(env.Get<subsys::Concurrency>()),
        logger_(env.GetOr<subsys::Logger>(NullLogger::kInstance)),
        ctx_(&ctx) {
    sdl_ = 0 == SDL_Init(SDL_INIT_VIDEO);
    if (!sdl_) {
      throw Exception {"SDL init failure"};
    }
    SetUpGL();
    SetUpWindow();
  } catch (const std::exception&) {
    TearDown();
    throw;
  }

 public:
  virtual ~Impl() = default;
  Impl(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl& operator=(Impl&&) = delete;

 public:
  TaskContext MakeContext() const noexcept {
    return TaskContext {win_, gl_};
  }
  void SchedulePolling() noexcept {
    if (nullptr != ctx_) {
      concurrency_->Push(nf7::SyncTask {
        clock_->now() + kPollingInterval,
        [this, self = shared_from_this()](auto&) { Poll(); },
      });
    }
  }
  void ScheduleTearDown() noexcept {
    ctx_ = nullptr;
    concurrency_->Exec(
        [this, self = shared_from_this()](auto&) { TearDown(); });
  }
  void Push(Task&& task) noexcept {
    const auto after = task.after();
    concurrency_->Push(SyncTask {
      after,
      [this, self = shared_from_this(), task = std::move(task)](auto&) mutable {
        auto ctx = MakeContext();
        task(ctx);
      },
    });
  }

 private:
  void Poll() noexcept {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (nullptr != ctx_) {
        ctx_->Notify(SDL_Event {e});
      }
    }
    SchedulePolling();
  }

 private:
  void SetUpGL() noexcept {
#   if defined(__APPLE__)
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#   else
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#   endif
  }
  void SetUpWindow() {
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
  void TearDown() noexcept {
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

 private:
  const std::shared_ptr<subsys::Clock> clock_;
  const std::shared_ptr<subsys::Concurrency> concurrency_;
  const std::shared_ptr<subsys::Logger> logger_;

  Context* ctx_;

  bool sdl_ {false};
  SDL_Window* win_ {nullptr};
  void* gl_ {nullptr};
};

Context::Context(Env& env)
    : subsys::Interface("nf7::core::gl3::Context"),
      impl_(std::make_shared<Impl>(env, *this)) {
  impl_->SchedulePolling();
}
Context::~Context() noexcept {
  impl_->ScheduleTearDown();
}
void Context::Push(Task&& task) noexcept {
  impl_->Push(std::move(task));
}

}  // namespace nf7::core::gl3
