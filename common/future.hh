#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "nf7.hh"

#include "common/generic_context.hh"


namespace nf7 {

// T must not be void, use std::monostate instead
template <typename T>
class Future final {
 public:
  class Promise;
  class Coro;
  class Awaiter;

  using Handle = std::coroutine_handle<Promise>;

  enum State { kYet, kDone, kError, };

  struct Data final {
   public:
    std::weak_ptr<nf7::Context> ctx;

    std::atomic<bool>   aborted = false;
    std::atomic<size_t> pros    = 0;
    std::atomic<State>  state   = kYet;

    std::mutex mtx;
    std::optional<T> value;
    std::exception_ptr exception;
    std::vector<std::function<void()>> recv;
  };
  class Promise final {
   public:
    template <typename U> friend class nf7::Future;
    template <typename U> friend class nf7::Future<U>::Coro;

    static constexpr bool kThisIsNf7FuturePromise = true;

    Promise() noexcept : data_(std::make_shared<Data>()) {
      ++data_->pros;
    }
    Promise(const std::shared_ptr<nf7::Context>& ctx) noexcept : Promise() {
      data_->ctx = ctx;
    }
    Promise(const Promise& src) noexcept : data_(src.data_) {
      ++data_->pros;
    }
    Promise(Promise&&) = default;
    Promise& operator=(const Promise& src) noexcept {
      data_ = src.data_;
      ++data_->pros;
    }
    Promise& operator=(Promise&&) = default;
    ~Promise() noexcept {
      if (data_ && --data_->pros == 0 && data_->state == kYet) {
        Throw(std::make_exception_ptr<nf7::Exception>({"promise forgotten"}));
      }
    }

    // thread-safe
    auto Wrap(const std::function<T()>& f) noexcept
    try {
      Return(f());
    } catch (Exception&) {
      Throw(std::current_exception());
    }
    // thread-safe
    auto Return(T&& v) {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (data_->state == kYet) {
        data_->state = kDone;
        data_->value = std::move(v);
        CallReceivers();
      }
    }
    // thread-safe
    void Throw(std::exception_ptr e) noexcept {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (data_->state == kYet) {
        data_->exception = e;
        data_->state     = kError;
        CallReceivers();
      }
    }

    Future future() const noexcept {
      assert(data_);
      return Future(data_);
    }

    auto get_return_object() noexcept {
      return Coro(Handle::from_promise(*this), data_);
    }
    auto initial_suspend() const noexcept {
      return std::suspend_always();
    }
    auto final_suspend() const noexcept {
      return std::suspend_always();
    }
    auto yield_value(const T& v) {
      Return(T(v));
      return std::suspend_never();
    }
    auto yield_value(T&& v) {
      Return(std::move(v));
      return std::suspend_never();
    }
    auto return_value(const T& v) {
      Return(T(v));
    }
    auto return_value(T&& v) {
      Return(std::move(v));
    }
    auto unhandled_exception() noexcept {
      Throw(std::current_exception());
    }

   private:
    std::shared_ptr<Data> data_;

    void CallReceivers() noexcept {
      for (auto recv : data_->recv) recv();
      data_->recv.clear();
    }
  };
  class Coro final {
   public:
    friend Promise;
    using promise_type = Promise;

    Coro() = delete;
    ~Coro() noexcept {
      if (data_) h_.destroy();
    }
    Coro(const Coro&) = delete;
    Coro(Coro&&) = default;
    Coro& operator=(const Coro&) = delete;
    Coro& operator=(Coro&&) = default;

    Future Start(const std::shared_ptr<nf7::Context>& ctx) noexcept {
      ctx->env().ExecSub(ctx, [ctx, h = h_]() { h.resume(); });
      data_->ctx = ctx;
      return Future(data_);
    }
    void Abort() noexcept {
      h_.promise().Throw(
          std::make_exception_ptr<nf7::Exception>({"coroutine aborted"}));
      data_->aborted = true;

      auto ctx = data_->ctx.lock();
      ctx->env().ExecSub(ctx, [h = h_]() { h.destroy(); });
    }

   private:
    Handle h_;
    std::shared_ptr<Data> data_;

    Coro(Handle h, const std::shared_ptr<Data>& data) noexcept : h_(h), data_(data) { }
  };
  class Awaiter final {
   public:
    Awaiter() = delete;
    Awaiter(Future& fu, const std::shared_ptr<nf7::Context>& ctx) noexcept :
        fu_(&fu), ctx_(ctx) {
    }
    Awaiter(const Awaiter&) = delete;
    Awaiter(Awaiter&&) = delete;
    Awaiter& operator=(const Awaiter&) = delete;
    Awaiter& operator=(Awaiter&&) = delete;

    bool await_ready() const noexcept { return !fu_->yet(); }
    template <typename U>
    void await_suspend(std::coroutine_handle<U> caller) const noexcept {
      static_assert(U::kThisIsNf7FuturePromise, "illegal coroutine");
      assert(fu_->data_);
      auto& data = *fu_->data_;

      std::unique_lock<std::mutex> k(data.mtx);
      if (fu_->yet()) {
        auto ctx = data.ctx.lock();
        assert(ctx);
        data.recv.push_back([caller, ctx]() {
          ctx->env().ExecSub(ctx, [caller]() {
            if (!caller.promise().data_->aborted) caller.resume();
          });
        });
      } else {
        // promise has ended after await_ready() is called
        caller.resume();
      }
    }
    auto await_resume() const {
      return fu_->value();
    }

   private:
    Future* const fu_;
    std::shared_ptr<nf7::Context> ctx_;
  };

  Future(const T& v) noexcept : imm_({v}) {
  }
  Future(T&& v) noexcept : imm_({std::move(v)}) {
  }
  Future(std::exception_ptr e) noexcept : imm_({e}) {
  }
  Future(const Future&) = default;
  Future(Future&&) = default;
  Future& operator=(const Future&) = default;
  Future& operator=(Future&&) = default;

  Future& Then(std::function<void(Future)>&& f) noexcept {
    if (data_) {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (yet()) {
        data_->recv.push_back(
            [d = data_, f = std::move(f)]() { f(Future(d)); });
        return *this;
      }
    }
    f(*this);
    return *this;
  }
  Future& ThenSub(const std::shared_ptr<nf7::Context>& ctx,
                  std::function<void(Future)>&&        f) noexcept {
    if (data_) {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (yet()) {
        data_->recv.push_back([d = data_, ctx, f = std::move(f)]() {
          ctx->env().ExecSub(ctx, std::bind(f, Future(d)));
        });
        return *this;
      }
    }
    ctx->env().ExecSub(ctx, std::bind(f, Future(data_)));
    return *this;
  }

  auto& value() {
    if (imm_) {
      if (std::holds_alternative<T>(*imm_)) return std::get<T>(*imm_);
      std::rethrow_exception(std::get<std::exception_ptr>(*imm_));
    }

    assert(data_);
    switch (data_->state) {
    case kYet:
      assert(false);
      break;
    case kDone:
      return *data_->value;
    case kError:
      std::rethrow_exception(data_->exception);
    }
    throw 0;
  }

  bool yet() const noexcept {
    return !imm_ && data_->state == kYet;
  }
  bool done() const noexcept {
    return (imm_ && std::holds_alternative<T>(*imm_)) || data_->state == kDone;
  }
  bool error() const noexcept {
    return (imm_ && std::holds_alternative<std::exception_ptr>(*imm_)) ||
        data_->state == kError;
  }

  Awaiter awaiter(const std::shared_ptr<nf7::Context>& ctx) noexcept {
    return Awaiter(*this, ctx);
  }

 private:
  std::optional<std::variant<T, std::exception_ptr>> imm_;
  std::shared_ptr<Data> data_;

  Future(const std::shared_ptr<Data>& data) noexcept : data_(data) { }
};

}  // namespace nf7
