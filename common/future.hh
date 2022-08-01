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

// How To Use (factory side)
// 1. Create Future<T>::Promise. (T is a type of returned value)
// 2. Get Future<T> from Future<T>::Promise and Pass it to ones who want to get T.
// 3. Call Promise::Return(T) or Promise::Throw() to finish the promise.
//
// Users who receive Future can wait for finishing
// by Future::Then(), Future::ThenSub(), or co_await.


// T must not be void, use std::monostate instead
template <typename T>
class Future final {
 public:
  class Promise;
  class Coro;

  using Handle = std::coroutine_handle<Promise>;

  enum State { kYet, kDone, kError, };

  // A data shared between Future, Promise, and Coro.
  // One per one Promise.
  struct Data final {
   public:
    std::weak_ptr<nf7::Context> ctx;

    std::atomic<bool>   destroyed = false;
    std::atomic<bool>   aborted   = false;
    std::atomic<size_t> pros      = 0;
    std::atomic<State>  state     = kYet;

    std::mutex mtx;
    std::optional<T> value;
    std::exception_ptr exception;
    std::vector<std::function<void()>> recv;
  };

  // Factory side have this to tell finish or abort.
  class Promise final {
   public:
    // Use data_() instead, MSVC doesn't allow this:
    // template <typename U> friend class nf7::Future<U>;
    // template <typename U> friend class nf7::Future<U>::Coro;

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
    auto Return(T&& v) noexcept {
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

    // thread-safe
    // Do Return(f()) if no exception is thrown, otherwise call Throw().
    auto Wrap(const std::function<T()>& f) noexcept
    try {
      Return(f());
    } catch (Exception&) {
      Throw(std::current_exception());
    }

    // thread-safe
    // Creates Future() object.
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
    auto return_void() {
      if (data_->state == kYet) {
        if constexpr (std::is_same<T, std::monostate>::value) {
          Return({});
        } else {
          assert(false && "coroutine returned without value");
        }
      }
    }
    auto unhandled_exception() noexcept {
      Throw(std::current_exception());
    }

    const std::shared_ptr<Data>& data__() noexcept { return data_; }

   private:
    std::shared_ptr<Data> data_;

    void CallReceivers() noexcept {
      for (auto recv : data_->recv) recv();
      data_->recv.clear();
    }
  };

  // Define a function returning Coro to implement a factory with coroutine.
  class Coro final {
   public:
    friend Promise;
    using promise_type = Promise;

    Coro() = delete;
    ~Coro() noexcept {
      if (data_ && !data_->destroyed.exchange(true)) {
        h_.destroy();
      }
    }
    Coro(const Coro&) = delete;
    Coro(Coro&&) = default;
    Coro& operator=(const Coro&) = delete;
    Coro& operator=(Coro&&) = default;

    Future Start(const std::shared_ptr<nf7::Context>& ctx) noexcept {
      ctx->env().ExecSub(ctx, [h = h_]() { h.resume(); });
      data_->ctx = ctx;
      return Future(data_);
    }
    void Abort() noexcept {
      h_.promise().Throw(
          std::make_exception_ptr<nf7::Exception>({"coroutine aborted"}));
      data_->aborted = true;
    }

   private:
    Handle h_;
    std::shared_ptr<Data> data_;

    Coro(Handle h, const std::shared_ptr<Data>& data) noexcept : h_(h), data_(data) { }
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

  // Schedules to execute f() immediately on any thread
  // when the promise is finished or aborted.
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

  // Schedules to execute f() as a sub task when the promise is finished or aborted.
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

  bool await_ready() const noexcept { return !yet(); }
  template <typename U>
  void await_suspend(std::coroutine_handle<U> caller) const noexcept {
    static_assert(U::kThisIsNf7FuturePromise, "illegal coroutine");
    assert(data_);
    auto& data = *data_;

    std::unique_lock<std::mutex> k(data.mtx);
    auto callee_ctx = data.ctx.lock();
    assert(callee_ctx);

    auto caller_data = caller.promise().data__();
    auto caller_ctx  = caller_data->ctx.lock();
    assert(caller_ctx);

    if (yet()) {
      data.recv.push_back([caller, caller_data, caller_ctx, callee_ctx]() {
        caller_ctx->env().ExecSub(caller_ctx, [caller, caller_data, caller_ctx]() {
          if (!caller_data->aborted) {
            caller.resume();
          } else {
            if (!caller_data->destroyed.exchange(true)) {
              caller.destroy();
            }
          }
        });
      });
    } else {
      // promise has ended after await_ready() is called
      caller.resume();
    }
  }
  auto await_resume() { return value(); }

 private:
  std::optional<std::variant<T, std::exception_ptr>> imm_;
  std::shared_ptr<Data> data_;

  Future(const std::shared_ptr<Data>& data) noexcept : data_(data) { }
};

}  // namespace nf7
