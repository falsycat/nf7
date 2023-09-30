// No copyright
#include "core/imgui/context.hh"

#include <SDL_opengl.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <chrono>
#include <utility>
#include <vector>
#include <iostream>

#include "iface/common/exception.hh"
#include "iface/common/observer.hh"
#include "iface/subsys/clock.hh"

#include "core/gl3/context.hh"


namespace nf7::core::imgui {

class Context::Impl final : public std::enable_shared_from_this<Impl> {
 private:
  static constexpr auto kUpdateInterval = std::chrono::milliseconds {33};

 private:
  class EventQueue final : public Observer<SDL_Event> {
   public:
    using Observer<SDL_Event>::Observer;

   public:
    EventQueue(const EventQueue&) = delete;
    EventQueue(EventQueue&&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    EventQueue& operator=(EventQueue&&) = delete;

   public:
    std::vector<SDL_Event> Take() noexcept { return std::move(items_); }

   private:
    void Notify(const SDL_Event& e) noexcept override { items_.push_back(e); }

   private:
    std::vector<SDL_Event> items_;
  };

 public:
  explicit Impl(Env& env)
      : clock_(env.Get<subsys::Clock>()),
        gl3_(env.Get<gl3::Context>()),
        events_(std::make_unique<EventQueue>(*gl3_)),
        imgui_(ImGui::CreateContext()) {
  }
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

  const std::shared_ptr<Driver>& Register(const std::shared_ptr<Driver>& driver)
  try {
    drivers_.emplace_back(driver);
    return driver;
  } catch (const std::bad_alloc&) {
    throw MemoryException {};
  }

 private:
  void Start(gl3::TaskContext& t) noexcept {
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplSDL2_InitForOpenGL(t.win(), t.gl());
    ImGui_ImplOpenGL3_Init(gl3::Context::kGlslVersion);
    Update(t);
  }
  void Update(gl3::TaskContext& t) noexcept {
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

    // draw something
    for (const auto& driver : drivers) { driver->Update(t); }
    ImGui::Render();

    // render them
    const auto& io = ImGui::GetIO();
    glViewport(0, 0,
               static_cast<int>(io.DisplaySize.x),
               static_cast<int>(io.DisplaySize.y));
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    for (const auto& driver : drivers) { driver->PreUpdate(t); }
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    for (const auto& driver : drivers) { driver->PostUpdate(t); }
    SDL_GL_SwapWindow(t.win());

    // schedule next frame
    gl3_->Push(gl3::Task {
      clock_->now() + kUpdateInterval,
      [wself = weak_from_this()](auto& t) {
        if (auto self = wself.lock()) { self->Update(t); }
      },
    });
  }
  void TearDown(gl3::TaskContext&) noexcept {
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(imgui_);
  }

 private:
  const std::shared_ptr<subsys::Clock> clock_;
  const std::shared_ptr<gl3::Context> gl3_;
  const std::unique_ptr<EventQueue> events_;

  ImGuiContext* const imgui_;

  std::vector<std::weak_ptr<Driver>> drivers_;
};

Context::Context(Env& env)
try : subsys::Interface("nf7::core::imgui::Context"),
      impl_(std::make_shared<Impl>(env)) {
  impl_->ScheduleStart();
} catch (const std::bad_alloc&) {
  throw MemoryException {};
}
Context::~Context() noexcept {
  impl_->ScheduleTearDown();
}
const std::shared_ptr<Driver>& Context::Register(
    const std::shared_ptr<Driver>& driver) {
  return impl_->Register(driver);
}

}  // namespace nf7::core::imgui
