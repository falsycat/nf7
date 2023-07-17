// No copyright
#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include "iface/common/exception.hh"
#include "iface/common/future.hh"

namespace nf7 {

class Task final {
 public:
  Task() = delete;
  explicit Task(
      std::function<void()>&& func,
      std::source_location location = std::source_location::current()) noexcept
      : func_(std::move(func)),
        location_(location) {
    assert(func_);
  }

  Task(const Task&) = delete;
  Task(Task&&) = default;
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&&) = default;

  void Run() {
    if (!func_) {
      throw Exception {"double run is not allowed", location_};
    }
    try {
      auto f = std::move(func_);
      f();
    } catch (...) {
      throw Exception {"task throws an exception", location_};
    }
  }

 private:
  std::function<void()> func_;

  std::source_location location_;
};

class TaskQueue : public std::enable_shared_from_this<TaskQueue> {
 public:
  TaskQueue() = default;
  virtual ~TaskQueue() = default;

  TaskQueue(const TaskQueue&) = delete;
  TaskQueue(TaskQueue&&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;
  TaskQueue& operator=(TaskQueue&&) = delete;

  // THREAD SAFE
  // an implementation must handle memory errors well
  virtual void Push(Task&&) noexcept = 0;

  // THREAD SAFE
  auto Wrap(
      auto&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    return [self = shared_from_this(), f = std::move(f), loc](auto&&... args) {
      self->Push(Task {[f = std::move(f),
                 ...args = std::forward<decltype(args)>(args)]() {
          f(std::forward<decltype(args)>(args)...);
      }, loc});
    };
  }

  // THREAD SAFE
  template <typename R>
  Future<R> RunAnd(
      std::function<R()>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    return RunAnd({}, std::move(f));
  }

  // THREAD SAFE
  template <typename R>
  Future<R> RunAnd(
      Future<R>::Completer&& comp,
      std::function<R()>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Future<R> future {comp};
    Push(Task {
         [f = std::move(f), comp = std::move(comp)]() { comp.Run(f); }, loc});
    return future;
  }

  // THREAD SAFE
  void Run(
      std::function<void()>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Push(Task {std::move(f), loc});
  }
};

}  // namespace nf7
