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
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "iface/common/exception.hh"
#include "iface/common/future.hh"

namespace nf7 {

template <typename... Args>
class Task final {
 public:
  using Time = std::chrono::sys_time<std::chrono::milliseconds>;
  using Function = std::function<void(Args&&...)>;

  Task() = delete;
  explicit Task(
      Function&& func,
      std::source_location location = std::source_location::current()) noexcept
      : func_(std::move(func)),
        location_(location) {
    assert(func_);
  }
  Task(
      Time after,
      Function&& func,
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

  void operator()(Args&&... args) {
    try {
      func_(std::forward<Args>(args)...);
    } catch (...) {
      throw Exception {"task throws an exception", location_};
    }
  }

  Time after() const noexcept { return after_; }
  std::source_location location() const noexcept { return location_; }

 private:
  Time after_;

  Function func_;

  std::source_location location_;
};

template <typename... Args>
class TaskQueue : public std::enable_shared_from_this<TaskQueue<Args...>> {
 public:
  using Item = Task<Args...>;

  TaskQueue() = default;
  virtual ~TaskQueue() = default;

  TaskQueue(const TaskQueue&) = delete;
  TaskQueue(TaskQueue&&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;
  TaskQueue& operator=(TaskQueue&&) = delete;

  // THREAD SAFE
  // an implementation must handle memory errors well
  virtual void Push(Item&&) noexcept = 0;

  // THREAD SAFE
  auto Wrap(Item&& task) noexcept {
    return [self = shared_from_this(), task = std::move(task)](auto&&...)
        mutable {
      self->Push(std::move(task));
    };
  }

  // THREAD SAFE
  auto Wrap(
      auto&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    using F = decltype(f);

    return [self = shared_from_this(), f = std::move(f), loc](auto&&... args1)
        mutable {
      self->Push(Item {
        [f = std::move(f), ...args1 = std::forward<decltype(args1)>(args1)]
        (auto&&... args2) mutable {
          if constexpr (
              std::is_invocable_v<F, decltype(args1)..., decltype(args2)...>) {
            f(std::forward<decltype(args1)>(args1)...,
              std::forward<decltype(args2)>(args2)...);
          } else if constexpr (std::is_invocable_v<F, decltype(args1)...>) {
            f(std::forward<decltype(args1)>(args1)...);
          } else {
            []<bool kValidFunction = false>() {
              static_assert(kValidFunction, "a function to wrap is invalid");
            }();
          }
        },
        loc,
      });
    };
  }

  // THREAD SAFE
  template <typename R>
  Future<R> ExecAnd(
      std::function<R(Args&&...)>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    return ExecAnd({}, std::move(f));
  }

  // THREAD SAFE
  template <typename R>
  Future<R> ExecAnd(
      Future<R>::Completer&& comp,
      std::function<R(Args&&...)>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Future<R> future {comp};
    Push(Item { [f = std::move(f), comp = std::move(comp)](Args&&...) mutable {
      comp.Exec(f);
    }, loc});
    return future;
  }

  // THREAD SAFE
  void Exec(
      std::function<void()>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Push(Item {std::move(f), loc});
  }

 private:
  using std::enable_shared_from_this<TaskQueue<Args...>>::shared_from_this;
};

template <typename T, typename... Args>
class WrappedTaskQueue : public T {
 public:
  static_assert(std::is_base_of_v<TaskQueue<Args...>, T>,
                "base type should be based on TaskQueue");

  using Inside = TaskQueue<Args...>;
  using Item   = Task<Args...>;

  WrappedTaskQueue() = delete;
  explicit WrappedTaskQueue(std::unique_ptr<Inside>&& q) noexcept
      : q_(std::move(q)) {
    assert(q_);
  }

  void Push(Item&& task) noexcept override {
    q_->Push(std::move(task));
  }

  using Inside::Wrap;
  using Inside::Exec;
  using Inside::ExecAnd;

 private:
  std::unique_ptr<Inside> q_;
};

template <typename... Args>
class SimpleTaskQueue : public TaskQueue<Args...> {
 public:
  using Item = Task<Args...>;
  using Time = Item::Time;

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

    virtual std::tuple<Args...> params() const noexcept = 0;
    virtual Time tick() const noexcept { return {}; }

    virtual bool nextIdleInterruption() const noexcept { return false; }
    virtual bool nextTaskInterruption() const noexcept { return false; }
  };

  SimpleTaskQueue() = default;

  void Push(Item&& task) noexcept override {
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
            std::apply(task, driver.params());
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
  bool CheckIfSleeping(Time now) const noexcept {
    return tasks_.empty() || tasks_.top().after() > now;
  }
  Time nextAwakeTime() const noexcept {
    return tasks_.empty()? Time::max(): tasks_.top().after();
  }

  std::mutex mtx_;
  std::condition_variable cv_;

  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> tasks_;
};

}  // namespace nf7
