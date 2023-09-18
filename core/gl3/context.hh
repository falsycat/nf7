// No copyright
#pragma once

#include <SDL.h>

#include <memory>
#include <utility>

#include "iface/subsys/clock.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/interface.hh"
#include "iface/subsys/logger.hh"
#include "iface/env.hh"


namespace nf7::core::gl3 {

class TaskContext final {
 public:
  TaskContext() = default;

  TaskContext(const TaskContext&) = delete;
  TaskContext(TaskContext&&) = delete;
  TaskContext& operator=(const TaskContext&) = delete;
  TaskContext& operator=(TaskContext&&) = delete;
};

using Task      = nf7::Task<TaskContext&>;
using TaskQueue = nf7::TaskQueue<Task>;


class Context : public subsys::Interface, public TaskQueue {
 public:
# if defined(__APPLE__)
    static constexpr const char* kGlslVersion = "#version 150";
# else
    static constexpr const char* kGlslVersion = "#version 130";
# endif

 public:
  class Impl;

 public:
  explicit Context(
      Env&,
      const std::shared_ptr<Impl>& = nullptr);
  ~Context() noexcept override;

 public:
  void Push(Task&&) noexcept override;

 private:
  const std::shared_ptr<Impl> impl_;
};

class Context::Impl : public std::enable_shared_from_this<Impl> {
 public:
  class TaskDriver;
  friend class Context;

 public:
  explicit Impl(Env&);
  virtual ~Impl() = default;

  Impl(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl& operator=(Impl&&) = delete;

 protected:
  virtual bool Update() noexcept {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { }

    SDL_GL_MakeCurrent(win_, gl_);
    SDL_GL_SwapWindow(win_);
    return true;
  }

 protected:
  SDL_Window* win() const noexcept { return win_; }
  void* gl() const noexcept { return gl_; }

 private:
  void Exit() noexcept { alive_ = false; }
  void Push(Task&& task) noexcept { tasq_.Push(std::move(task)); }
  void Main() noexcept;

 private:
  void SetUpGL() noexcept;
  void SetUpWindow();
  void TearDown() noexcept;

 private:
  const std::shared_ptr<subsys::Clock> clock_;
  const std::shared_ptr<subsys::Concurrency> concurrency_;
  const std::shared_ptr<subsys::Logger> logger_;

  nf7::SimpleTaskQueue<Task> tasq_;
  bool alive_ {true};

  bool sdl_ {false};
  SDL_Window* win_ {nullptr};
  void* gl_ {nullptr};
};

}  // namespace nf7::core::gl3
