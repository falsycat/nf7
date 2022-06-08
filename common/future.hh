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

template <typename T>
class Future final {
 public:
  static constexpr bool kVoid = std::is_same<T, void>::value;

  class Promise;
  class Coro;
  class Awaiter;

  using Handle = std::coroutine_handle<Promise>;
  using Return = typename std::conditional<kVoid, int, T>::type;

  enum State { kYet, kDone, kError, };

  struct Data final {
   public:
    std::atomic<bool>   aborted = false;
    std::atomic<size_t> pros    = 0;
    std::atomic<State>  state   = kYet;

    std::mutex mtx;
    std::optional<Return> value;
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
    auto Wrap(const std::function<T()>& f) noexcept requires(!kVoid)
    try {
      Return(f());
    } catch (Exception&) {
      Throw(std::current_exception());
    }
    // thread-safe
    auto Return(T&& v) requires(!kVoid) {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (data_->state == kYet) {
        data_->state = kDone;
        data_->value = std::move(v);
        for (auto recv : data_->recv) recv();
      }
    }
    // thread-safe
    auto Return(int = 0) requires(kVoid) {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (data_->state == kYet) {
        data_->state = kDone;
        for (auto recv : data_->recv) recv();
      }
    }
    // thread-safe
    void Throw(std::exception_ptr e) noexcept {
      std::unique_lock<std::mutex> k(data_->mtx);
      if (data_->state == kYet) {
        data_->exception = e;
        data_->state     = kError;
        for (auto recv : data_->recv) recv();
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
    auto yield_value(const T& v) requires(!kVoid) {
      Return(T(v));
      return std::suspend_never();
    }
    auto yield_value(T&& v) requires(!kVoid) {
      Return(std::move(v));
      return std::suspend_never();
    }
    auto return_void() {
      if constexpr (kVoid) Return();
      return std::suspend_never();
    }
    auto unhandled_exception() noexcept {
      Throw(std::current_exception());
    }

   private:
    std::shared_ptr<Data> data_;
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

    Future Start(File& f, std::string_view desc) noexcept {
      auto ctx = std::make_shared<nf7::GenericContext>(f.env(), f.id());
      ctx->description() = desc;
      return Start(ctx);
    }
    Future Start(const std::shared_ptr<nf7::Context>& ctx) noexcept {
      ctx->env().ExecSub(ctx, [ctx, h = h_]() { h.resume(); });
      return Future(data_);
    }
    void Abort(const std::shared_ptr<nf7::Context>& ctx) noexcept {
      h_.promise().Throw(
          std::make_exception_ptr<nf7::Exception>({"coroutine aborted"}));
      data_->aborted = true;
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

      std::unique_lock<std::mutex> k(fu_->data_->mtx);
      if (fu_->yet()) {
        fu_->data_->recv.push_back([caller, ctx = ctx_]() {
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
      if constexpr (!kVoid) {
        if (std::holds_alternative<Return>(fu_->imm_)) {
          return std::move(std::get<Return>(fu_->imm_));
        }
      }
      if (std::holds_alternative<std::exception_ptr>(fu_->imm_)) {
        std::rethrow_exception(std::get<std::exception_ptr>(fu_->imm_));
      }

      assert(fu_->data_);
      switch (fu_->data_->state) {
      case kDone:
        if constexpr (kVoid) {
          return;
        } else {
          return std::move(*fu_->data_->value);
        }
      case kError:
        std::rethrow_exception(fu_->data_->exception);
      default:
        assert(false);
        throw 0;
      }
    }

   private:
    Future* const fu_;
    std::shared_ptr<nf7::Context> ctx_;
  };

  Future() = delete;
  template <typename U=T> requires (!kVoid)
  Future(T&& v) noexcept : imm_(std::move(v)) {
  }
  Future(std::exception_ptr e) noexcept : imm_(e) {
  }
  Future(const Future&) = default;
  Future(Future&&) = default;
  Future& operator=(const Future&) = default;
  Future& operator=(Future&&) = default;

  bool yet() const noexcept {
    return std::holds_alternative<std::monostate>(imm_) && data_->state == kYet;
  }
  bool done() const noexcept {
    return std::holds_alternative<Return>(imm_) || data_->state == kDone;
  }
  bool error() const noexcept {
    return std::holds_alternative<std::exception_ptr>(imm_) || data_->state == kError;
  }

  Awaiter awaiter(const std::shared_ptr<nf7::Context>& ctx) noexcept {
    return Awaiter(*this, ctx);
  }

 private:
  std::variant<std::monostate, Return, std::exception_ptr> imm_;
  std::shared_ptr<Data> data_;

  Future(const std::shared_ptr<Data>& data) noexcept : data_(data) { }
};

}  // namespace nf7
