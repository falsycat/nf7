// No copyright
#pragma once

#include <cassert>
#include <chrono>
#include <concepts>
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

template <typename T>
concept TaskLike = requires (T& x) {
  typename T::Param;
  typename T::Time;

  T([](typename T::Param){}, std::source_location {});
  requires std::copy_constructible<T>;
  requires std::move_constructible<T>;
  requires std::invocable<T, typename T::Param>;

  {x.operator<=>(x)} noexcept;
  {x.after()} noexcept -> std::convertible_to<typename T::Time>;
};

template <typename T, typename Item>
concept TaskDriverLike = requires (T& x, Item&& y) {
  requires TaskLike<Item>;

  {x.BeginBusy()} noexcept;
  {x.Drive(std::move(y))} noexcept;
  {x.EndBusy()} noexcept;
  {x.tick()} noexcept -> std::convertible_to<typename Item::Time>;
  {x.nextIdleInterruption()} noexcept -> std::convertible_to<bool>;
  {x.nextTaskInterruption()} noexcept -> std::convertible_to<bool>;
};


template <typename P>
class Task final {
 public:
  using Param = P;
  using Time  = std::chrono::sys_time<std::chrono::milliseconds>;
  using Func  = std::function<void(Param)>;

  Task() = delete;
  explicit Task(
      Func&& func,
      std::source_location location = std::source_location::current()) noexcept
      : func_(std::move(func)),
        location_(location) {
    assert(func_);
  }
  Task(
      Time after,
      Func&& func,
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

  void operator()(Param param) {
    try {
      func_(std::forward<Param>(param));
    } catch (...) {
      throw Exception {"task throws an exception", location_};
    }
  }

  Time after() const noexcept { return after_; }
  std::source_location location() const noexcept { return location_; }

 private:
  Time after_;
  Func func_;
  std::source_location location_;
};

template <TaskLike T>
class TaskQueue : public std::enable_shared_from_this<TaskQueue<T>> {
 public:
  using Item  = T;
  using Param = typename Item::Param;

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
    return [self = shared_from_this(), f = std::move(f), loc](auto&&... args)
        mutable {
      using F = decltype(f);
      self->Push(Item {
        [f = std::move(f), ...args = std::forward<decltype(args)>(args)]
        (Param p) mutable {
          if constexpr (std::is_invocable_v<F, decltype(args)..., Param>) {
            f(std::forward<decltype(args)>(args)..., std::forward<Param>(p));
          } else if constexpr (std::is_invocable_v<F, decltype(args)...>) {
            f(std::forward<decltype(args)>(args)...);
          } else {
            static_assert(false, "the wrapped function is invalid");
          }
        },
        loc,
      });
    };
  }

  // THREAD SAFE
  template <typename R>
  Future<R> ExecAnd(
      std::function<R(Param)>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    return ExecAnd({}, std::move(f));
  }

  // THREAD SAFE
  template <typename R>
  Future<R> ExecAnd(
      Future<R>::Completer&& cmp,
      std::function<R(Param)>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Future<R> future {cmp};
    Push(Item { [f = std::move(f), cmp = std::move(cmp)](Param) mutable {
      cmp.Exec(f);
    }, loc});
    return future;
  }

  // THREAD SAFE
  void Exec(
      std::function<void(Param)>&& f,
      std::source_location loc = std::source_location::current()) noexcept {
    Push(Item {std::move(f), loc});
  }

 protected:
  using std::enable_shared_from_this<TaskQueue<Item>>::shared_from_this;
};

template <typename I>
class WrappedTaskQueue : public I {
 public:
  static_assert(std::is_base_of_v<TaskQueue<typename I::Item>, I>,
                "base type should be based on TaskQueue");

  using Item   = typename I::Item;
  using Inside = TaskQueue<Item>;

  WrappedTaskQueue() = delete;
  explicit WrappedTaskQueue(const std::shared_ptr<Inside>& q) noexcept : q_(q) {
    assert(q_);
  }

  void Push(Item&& task) noexcept override {
    q_->Push(std::move(task));
  }

  using Inside::Wrap;
  using Inside::Exec;
  using Inside::ExecAnd;

 private:
  std::shared_ptr<Inside> q_;
};

template <TaskLike T>
class SimpleTaskQueue : public TaskQueue<T> {
 public:
  using Item = T;
  using Time = typename Item::Time;

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

  // THREAD-SAFE
  bool WaitForEmpty(auto dur) noexcept {
    std::unique_lock<std::mutex> k {mtx_};
    return cv_.wait_for(k, dur, [this]() { return tasks_.empty(); });
  }

  template <TaskDriverLike<Item> Driver>
  void Drive(Driver& driver) {
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

          driver.Drive(std::move(task));
        }
      } catch (const std::system_error&) {
        driver.EndBusy();
        throw Exception {"mutex error"};
      }
      driver.EndBusy();

      try {
        std::unique_lock<std::mutex> k{mtx_};
        cv_.notify_all();

        const auto until = nextAwake();
        const auto pred  = [&]() {
          return
            !CheckIfSleeping(driver.tick()) ||
            until.value_or(Time::max()) > nextAwake().value_or(Time::max()) ||
            driver.nextIdleInterruption();
        };
        if (std::nullopt != until) {
          cv_.wait_for(k, *until - driver.tick(), pred);
        } else {
          cv_.wait(k, pred);
        }
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
  std::optional<Time> nextAwake() const noexcept {
    if (tasks_.empty()) {
      return std::nullopt;
    }
    return tasks_.top().after();
  }

  std::mutex mtx_;
  std::condition_variable cv_;

  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> tasks_;
};

}  // namespace nf7
