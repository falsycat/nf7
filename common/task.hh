#pragma once

#include <memory>
#include <optional>

#include "nf7.hh"

#include "common/future.hh"


namespace nf7 {

template <typename T>
class Task : public nf7::Context,
    public std::enable_shared_from_this<Task<T>> {
 public:
  class Holder;

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
  auto fu() noexcept { return *fu_; }

 protected:
  virtual nf7::Future<T>::Coro Proc() noexcept = 0;

 private:
  std::optional<typename nf7::Future<T>::Coro> coro_;
  std::optional<nf7::Future<T>>                fu_;
};

template <typename T>
class Task<T>::Holder final {
 public:
  Holder() = default;
  Holder(const std::shared_ptr<Task<T>>& ctx) noexcept : ctx_(ctx) {
  }
  ~Holder() noexcept {
    Abort();
  }
  Holder(const Holder&) = delete;
  Holder(Holder&& src) noexcept = default;
  Holder& operator=(const Holder&) = delete;
  Holder& operator=(Holder&& src) noexcept {
    if (this != &src) {
      Abort();
      ctx_ = std::move(src.ctx_);
    }
    return *this;
  }

  void Abort() noexcept {
    if (auto ctx = ctx_.lock()) ctx->Abort();
    ctx_ = {};
  }

  std::shared_ptr<Task<T>> lock() const noexcept { return ctx_.lock(); }

 private:
  std::weak_ptr<Task<T>> ctx_;
};

}  // namespace nf7
