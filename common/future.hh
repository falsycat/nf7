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
// by Future::Then(), Future::ThenIf(), Future::Catch(), or co_await.


class CoroutineAbortException final : public nf7::Exception {
 public:
  using nf7::Exception::Exception;
};


template <typename T>
class Future final {
 public:
  class Promise;
  class Coro;

  using ThisFuture = nf7::Future<T>;
  using Handle     = std::coroutine_handle<Promise>;
  using Imm        = std::variant<T, std::exception_ptr>;

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
    // Use data_() instead, MSVC can't understand the followings:
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
        data_->value = std::move(v);
        data_->state = kDone;
        CallReceivers();
      }
    }
    auto Return(const T& v) noexcept {
      Return(T {v});
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
    template <typename E, typename... Args>
    void Throw(Args&&... args) noexcept {
      return Throw(std::make_exception_ptr<E>(E {std::forward<Args>(args)...}));
    }

    // thread-safe
    // Do Return(f()) if no exception is thrown, otherwise call Throw().
    auto Wrap(const std::function<T()>& f) noexcept
    try {
      Return(f());
    } catch (...) {
      Throw(std::current_exception());
    }

    // thread-safe
    // Creates Future() object.
    ThisFuture future() const noexcept {
      assert(data_);
      return ThisFuture(data_);
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

    ThisFuture Start(const std::shared_ptr<nf7::Context>& ctx) noexcept {
      ctx->env().ExecSub(ctx, [h = h_]() { h.resume(); });
      data_->ctx = ctx;
      return ThisFuture(data_);
    }
    void Abort() noexcept {
      h_.promise().Throw(
          std::make_exception_ptr<CoroutineAbortException>({"coroutine aborted"}));
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
  Future(const Imm& imm) noexcept : imm_(imm) {
  }
  Future(Imm&& imm) noexcept : imm_(std::move(imm)) {
  }
  Future(const ThisFuture&) = default;
  Future(ThisFuture&&) = default;
  Future& operator=(const ThisFuture&) = default;
  Future& operator=(ThisFuture&&) = default;

  // Schedules to execute f() immediately on any thread
  // when the promise is finished or aborted.
  // If ctx is not nullptr, the function will be run synchronized with main thread.
  ThisFuture& Then(const std::shared_ptr<nf7::Context>& ctx, std::function<void(ThisFuture&)>&& f) noexcept {
    auto fun = std::move(f);
    if (ctx) {
      fun = [ctx, fun = std::move(fun)](auto& fu) {
        ctx->env().ExecSub(
            ctx, [fu = fu, fun = std::move(fun)]() mutable { fun(fu); });
      };
    }
    if (data_) {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (yet()) {
        data_->recv.push_back(
            [fun = std::move(fun), fu = ThisFuture {data_}]() mutable { fun(fu); });
        return *this;
      }
    }
    fun(*this);
    return *this;
  }
  template <typename R>
  nf7::Future<R> Then(const std::shared_ptr<nf7::Context>& ctx,
                      std::function<void(ThisFuture&, typename nf7::Future<R>::Promise&)>&& f) noexcept {
    typename nf7::Future<R>::Promise pro;
    Then(ctx, [pro, f = std::move(f)](auto& fu) mutable {
      try {
        f(fu, pro);
      } catch (...) {
        pro.Throw(std::current_exception());
      }
    });
    return pro.future();
  }
  ThisFuture& Then(auto&& f) noexcept {
    return Then(nullptr, std::move(f));
  }

  // same as Then() but called when it's done without error
  ThisFuture& ThenIf(const std::shared_ptr<nf7::Context>& ctx, std::function<void(T&)>&& f) noexcept {
    Then(ctx, [f = std::move(f)](auto& fu) {
      if (fu.done()) f(fu.value());
    });
    return *this;
  }
  ThisFuture& ThenIf(auto&& f) noexcept {
    return ThenIf(nullptr, std::move(f));
  }

  // same as Then() but called when it caused an exception
  template <typename E>
  ThisFuture& Catch(const std::shared_ptr<nf7::Context>& ctx, std::function<void(E&)>&& f) noexcept {
    Then(ctx, [f = std::move(f)](auto& fu) {
      try { fu.value(); } catch (E& e) { f(e); }
    });
    return *this;
  }
  template <typename E>
  ThisFuture& Catch(auto&& f) noexcept {
    return Catch<E>(nullptr, std::move(f));
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
  auto& await_resume() { return value(); }

 private:
  std::optional<Imm> imm_;
  std::shared_ptr<Data> data_;

  Future(const std::shared_ptr<Data>& data) noexcept : data_(data) { }
};

}  // namespace nf7
