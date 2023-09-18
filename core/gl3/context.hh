// No copyright
#pragma once

#include <SDL.h>

#include <memory>
#include <utility>

#include "iface/common/observer.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/interface.hh"
#include "iface/env.hh"


namespace nf7::core::gl3 {

class TaskContext final {
 public:
  TaskContext() = delete;
  TaskContext(SDL_Window* win, void* gl) noexcept
      : win_(win), gl_(gl) { }

  TaskContext(const TaskContext&) = delete;
  TaskContext(TaskContext&&) = delete;
  TaskContext& operator=(const TaskContext&) = delete;
  TaskContext& operator=(TaskContext&&) = delete;

 public:
  SDL_Window* win() const noexcept { return win_; }
  void* gl() const noexcept { return gl_; }

 private:
  SDL_Window* const win_;
  void* const gl_;
};

using Task      = nf7::Task<TaskContext&>;
using TaskQueue = nf7::TaskQueue<Task>;


class Context :
    public subsys::Interface,
    public Observer<SDL_Event>::Target,
    public TaskQueue {
 public:
# if defined(__APPLE__)
    static constexpr const char* kGlslVersion = "#version 150";
# else
    static constexpr const char* kGlslVersion = "#version 130";
# endif

 public:
  class Impl;

 public:
  explicit Context(Env&);
  ~Context() noexcept override;

 public:
  // THREAD-SAFE
  void Push(Task&&) noexcept override;

 private:
  const std::shared_ptr<Impl> impl_;
};

}  // namespace nf7::core::gl3
