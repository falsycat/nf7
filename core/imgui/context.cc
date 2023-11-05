// No copyright
#include "core/imgui/context.hh"

#include <SDL_opengl.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "iface/common/exception.hh"
#include "iface/common/observer.hh"
#include "iface/subsys/clock.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/logger.hh"

#include "core/gl3/context.hh"
#include "core/imgui/luajit_driver.hh"
#include "core/luajit/context.hh"
#include "core/logger.hh"


namespace nf7::core::imgui {

class Context::Impl final : public std::enable_shared_from_this<Impl> {
 private:
  static constexpr auto kUpdateInterval = std::chrono::milliseconds {33};

 private:
  using Tasq = SimpleTaskQueue<SyncTask>;

 private:
  class EventQueue final : public Observer<SDL_Event> {
   public:
    using Observer<SDL_Event>::Observer;

   public:
    std::vector<SDL_Event> Take() noexcept { return std::move(items_); }
    void Notify(const SDL_Event& e) noexcept override { items_.push_back(e); }

   private:
    std::vector<SDL_Event> items_;
  };

 private:
  class SwitchingTasq : public subsys::Concurrency {
   public:
    SwitchingTasq(
        const std::weak_ptr<Tasq>& primary,
        const std::shared_ptr<subsys::Concurrency>& secondary) noexcept
        : primary_(primary), secondary_(secondary) { }

   public:
    void Push(SyncTask&& item) noexcept override {
      if (auto primary = primary_.lock()) {
        primary->Push(std::move(item));
      } else {
        secondary_->Push(std::move(item));
      }
    }

   private:
    const std::weak_ptr<Tasq> primary_;
    const std::shared_ptr<subsys::Concurrency> secondary_;
  };

 public:
  explicit Impl(Env& env)
      : concurrency_(env.Get<subsys::Concurrency>()),
        clock_(env.Get<subsys::Clock>()),
        gl3_(env.Get<gl3::Context>()),
        logger_(env.GetOr<subsys::Logger>(NullLogger::kInstance)),
        events_(std::make_unique<EventQueue>(*gl3_)),
        tasq_(std::make_shared<Tasq>()),
        tasq_wrap_(std::make_shared<SwitchingTasq>(tasq_, concurrency_)),
        ljctx_(luajit::Context::MakeSync(
            *LazyEnv::Make(
                {{typeid(subsys::Concurrency), tasq_wrap_}},
                env.self()))),
        imgui_(ImGui::CreateContext()) { }

  Impl(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl& operator=(Impl&&) = delete;

 public:
  void ScheduleStart() noexcept {
    gl3_->Exec([this, self = shared_from_this()](auto& t) { Start(t); });
  }
  void ScheduleTearDown() noexcept {
    gl3_->Exec([this, self = shared_from_this()](auto& t) { TearDown(t); });
  }

  const std::shared_ptr<Driver>& Register(const std::shared_ptr<Driver>& driver) {
    drivers_.emplace_back(driver);
    return driver;
  }

  std::shared_ptr<Env> MakeDriversEnv(const std::shared_ptr<Env>& env) {
    return LazyEnv::Make(
        {
          {typeid(subsys::Concurrency), tasq_wrap_},
          {typeid(luajit::Context), ljctx_},
        },
        env);
  }

  Future<std::shared_ptr<luajit::Value>> MakeLuaExtension() noexcept
  try {
    if (std::nullopt == ljext_) {
      ljext_.emplace();
      ljext_->RunAsync(ljctx_, concurrency_, LuaJITDriver::MakeExtensionObject);
    }
    return ljext_->future();
  } catch (const std::exception&) {
    return {std::current_exception()};
  }

 private:
  void Start(gl3::TaskContext& t) noexcept {
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplSDL2_InitForOpenGL(t.win(), t.gl());
    ImGui_ImplOpenGL3_Init(gl3::Context::kGlslVersion);
    Update(t);
  }
  void Update(gl3::TaskContext& t) noexcept {
    ConsumeTasks();
    ImGui::SetCurrentContext(imgui_);

    // get active drivers and drop dead ones
    std::vector<std::shared_ptr<Driver>> drivers;
    for (auto itr = drivers_.begin(); itr != drivers_.end();) {
      if (auto driver = itr->lock()) {
        drivers.emplace_back(std::move(driver));
        ++itr;
      } else {
        itr = drivers_.erase(itr);
      }
    }

    // event handling
    const auto events = events_->Take();
    for (const auto& event : events) {
      ImGui_ImplSDL2_ProcessEvent(&event);
    }

    // frame reset
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // pre processing
    for (const auto& driver : drivers) {
      driver->PreUpdate(t);
      ConsumeTasks();
    }

    // draw UI
    for (const auto& driver : drivers) {
      driver->Update(t);
      ConsumeTasks();
    }
    ImGui::Render();

    // clear the display and render them
    const auto& io = ImGui::GetIO();
    glViewport(0, 0,
               static_cast<int>(io.DisplaySize.x),
               static_cast<int>(io.DisplaySize.y));
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // post processing
    for (const auto& driver : drivers) {
      driver->PostUpdate(t);
      ConsumeTasks();
    }

    // update window
    SDL_GL_SwapWindow(t.win());

    // schedule next frame
    gl3_->Push(gl3::Task {
      clock_->now() + kUpdateInterval,
      [wself = weak_from_this()](auto& t) {
        if (auto self = wself.lock()) { self->Update(t); }
      },
    });
  }
  void ConsumeTasks() noexcept {
    class A final {
     public:
      A(const std::shared_ptr<subsys::Logger>& logger,
        SyncTask::Time tick) noexcept
          : logger_(logger), tick_(tick) { }

     public:
      void BeginBusy() noexcept { }
      void EndBusy() noexcept { idle_ = true; }
      void Drive(SyncTask&& task) noexcept
      try {
        SyncTaskContext ctx;
        task(ctx);
      } catch (const std::exception&) {
        logger_->Warn("error caused by a task came from ImGui driver");
      }
      SyncTask::Time tick() const noexcept { return tick_; }
      bool nextIdleInterruption() const noexcept { return idle_; }
      bool nextTaskInterruption() const noexcept { return false; }

     private:
      const std::shared_ptr<subsys::Logger> logger_;
      const SyncTask::Time tick_;

      bool idle_ = false;
    };
    A driver {logger_, clock_->now()};
    tasq_->Drive(driver);
  }
  void TearDown(gl3::TaskContext&) noexcept {
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(imgui_);
  }

 private:
  const std::shared_ptr<subsys::Concurrency> concurrency_;
  const std::shared_ptr<subsys::Clock>       clock_;
  const std::shared_ptr<gl3::Context>        gl3_;
  const std::shared_ptr<subsys::Logger>      logger_;
  const std::unique_ptr<EventQueue>          events_;

  const std::shared_ptr<Tasq>          tasq_;
  const std::shared_ptr<SwitchingTasq> tasq_wrap_;

  const std::shared_ptr<luajit::Context> ljctx_;
  std::optional<Future<std::shared_ptr<luajit::Value>>::Completer> ljext_;

  ImGuiContext* const imgui_;
  std::vector<std::weak_ptr<Driver>> drivers_;
};

Context::Context(Env& env)
    : subsys::Interface("nf7::core::imgui::Context"),
      impl_(std::make_shared<Impl>(env)) {
  impl_->ScheduleStart();
}
Context::~Context() noexcept {
  impl_->ScheduleTearDown();
}
const std::shared_ptr<Driver>& Context::Register(
    const std::shared_ptr<Driver>& driver) {
  return impl_->Register(driver);
}
std::shared_ptr<Env> Context::MakeDriversEnv(const std::shared_ptr<Env>& env) {
  return impl_->MakeDriversEnv(env);
}
Future<std::shared_ptr<luajit::Value>> Context::MakeLuaExtension() noexcept {
  return impl_->MakeLuaExtension();
}

}  // namespace nf7::core::imgui
