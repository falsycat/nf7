// No copyright
#pragma once

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "iface/common/exception.hh"
#include "iface/common/future.hh"

namespace nf7 {

class Task final {
 public:
  using Time = std::chrono::sys_time<std::chrono::milliseconds>;

  Task() = delete;
  explicit Task(
      std::function<void()>&& func,
      std::source_location location = std::source_location::current()) noexcept
      : func_(std::move(func)),
        location_(location) {
    assert(func_);
  }
  Task(
      Time after,
      std::function<void()>&& func,
      std::source_location location = std::source_location::current()) noexcept
      : after_(after),
        func_(std::move(func)),
        location_(location) {
    assert(func_);
  }

  Task(const Task&) = default;
  Task(Task&&) = default;
  Task& operator=(const Task&) = default;
  Task& operator=(Task&&) = default;

  auto operator<=>(const Task& other) const noexcept {
    return after_ <=> other.after_;
  }

  void Exec() {
    try {
      auto f = std::move(func_);
      f();
    } catch (...) {
      throw Exception {"task throws an exception", location_};
    }
  }

  Time after() const noexcept { return after_; }
  std::source_location location() const noexcept { return location_; }

 private:
  Time after_;

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
  Future<R> ExecAnd(
      std::function<R()>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    return ExecAnd({}, std::move(f));
  }

  // THREAD SAFE
  template <typename R>
  Future<R> ExecAnd(
      Future<R>::Completer&& comp,
      std::function<R()>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Future<R> future {comp};
    Push(Task {
         [f = std::move(f), comp = std::move(comp)]() { comp.Exec(f); }, loc});
    return future;
  }

  // THREAD SAFE
  void Exec(
      std::function<void()>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Push(Task {std::move(f), loc});
  }
};

class SimpleTaskQueue : public TaskQueue {
 public:
  class Driver {
   public:
    Driver() = default;
    virtual ~Driver() = default;

    Driver(const Driver&) = delete;
    Driver(Driver&&) = delete;
    Driver& operator=(const Driver&) = delete;
    Driver& operator=(Driver&&) = delete;

    virtual void BeginBusy() noexcept { }
    virtual void EndBusy() noexcept { }

    virtual Task::Time tick() const noexcept { return {}; }
    virtual bool nextIdleInterruption() const noexcept { return false; }
    virtual bool nextTaskInterruption() const noexcept { return false; }
  };

  SimpleTaskQueue() = default;

  void Push(Task&& task) noexcept override {
    const auto location = task.location();
    try {
      std::unique_lock<std::mutex> k {mtx_};
      tasks_.push(std::move(task));
      cv_.notify_all();
    } catch (...) {
      onErrorWhilePush(location);
    }
  }

  // THREAD-SAFE
  void Wake() noexcept {
    std::unique_lock<std::mutex> k {mtx_};
    cv_.notify_all();
  }

  template <
    typename T,
    typename = std::enable_if<std::is_base_of_v<Driver, T>, void>>
  void Drive(T& driver) {
    while (!driver.nextIdleInterruption()) {
      driver.BeginBusy();
      try {
        while (!driver.nextTaskInterruption()) {
          std::unique_lock<std::mutex> k {mtx_};
          if (CheckIfSleeping(driver.tick())) {
            break;
          }
          auto task = tasks_.top();
          tasks_.pop();
          k.unlock();

          try {
            task.Exec();
          } catch (...) {
            onErrorWhileExec(task.location());
          }
        }
      } catch (const std::system_error&) {
        driver.EndBusy();
        throw Exception {"mutex error"};
      }
      driver.EndBusy();

      try {
        std::unique_lock<std::mutex> k{mtx_};

        const auto until = nextAwakeTime();
        const auto dur   = until - driver.tick();
        cv_.wait_for(k, dur, [&]() {
          return
            !CheckIfSleeping(driver.tick()) ||
            until > nextAwakeTime() ||
            driver.nextIdleInterruption();
        });
      } catch (const std::system_error&) {
        throw Exception {"mutex error"};
      }
    }
  }

 protected:
  // THREAD-SAFE
  virtual void onErrorWhilePush(std::source_location) noexcept { }

  // rethrowing aborts Drive()
  virtual void onErrorWhileExec(std::source_location) { }

 private:
  bool CheckIfSleeping(Task::Time now) const noexcept {
    return tasks_.empty() || tasks_.top().after() > now;
  }
  Task::Time nextAwakeTime() const noexcept {
    return tasks_.empty()? Task::Time::max(): tasks_.top().after();
  }

  std::mutex mtx_;
  std::condition_variable cv_;

  std::priority_queue<Task, std::vector<Task>, std::greater<Task>> tasks_;
};

}  // namespace nf7
