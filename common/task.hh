#pragma once

#include <memory>
#include <optional>
#include <utility>

#include "nf7.hh"

#include "common/future.hh"


namespace nf7 {

template <typename T>
class Task : public nf7::Context,
    public std::enable_shared_from_this<Task<T>> {
 public:
  class Holder;

  using Future = nf7::Future<T>;
  using Coro   = typename Future::Coro;

  using nf7::Context::Context;

  Task(const Task&) = delete;
  Task(Task&&) = delete;
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&&) = delete;

  void Start() noexcept {
    coro_ = Proc();
    fu_   = coro_->Start(self());
  }
  void Abort() noexcept {
    coro_->Abort();
  }

  auto self() noexcept {
    return std::enable_shared_from_this<Task<T>>::shared_from_this();
  }
  std::optional<Future>& fu() noexcept { return *fu_; }

 protected:
  virtual Coro Proc() noexcept = 0;

 private:
  std::optional<Coro>   coro_;
  std::optional<Future> fu_;
};

// all operations are not thread-safe
template <typename T>
class Task<T>::Holder final {
 public:
  Holder() = default;
  ~Holder() noexcept {
    Abort();
  }

  Holder(const Holder&) = delete;
  Holder(Holder&&) = delete;
  Holder& operator=(const Holder&) = delete;
  Holder& operator=(Holder&&) = delete;

  bool CleanUp() noexcept {
    return !!std::exchange(fu_, std::nullopt);
  }
  void Abort() noexcept {
    if (auto task = task_.lock()) {
      task->Abort();
    }
  }

  template <typename U, typename... Args>
  nf7::Future<T> StartIf(Args&&... args) noexcept {
    if (fu_) return *fu_;

    auto task = std::make_shared<U>(std::forward<Args>(args)...);
    task->Start();

    task_ = task;
    fu_   = task->fu();
    return *fu_;
  }

  std::optional<nf7::Future<T>>& fu() noexcept { return fu_; }
  const std::optional<nf7::Future<T>>& fu() const noexcept { return fu_; }

 private:
  std::weak_ptr<Task<T>> task_;

  std::optional<nf7::Future<T>> fu_;
};

}  // namespace nf7
