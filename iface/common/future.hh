// No copyright
#pragma once

#include <cassert>
#include <exception>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "iface/common/exception.hh"

namespace nf7 {

template <typename T>
concept FutureLike = requires () {
  typename T::Completer;
  { (typename T::Completer {}).future() } -> std::same_as<T>;
};

template <typename T>
class Future final {
 public:
  // !! Listener MUST NOT throw any exceptions !!
  class Internal;
  using Listener = std::function<void(const Internal&)>;

  class Completer;
  class Internal final {
   public:
    Internal() = default;
    Internal(T&& v) noexcept : result_(std::move(v)) { }
    explicit Internal(std::exception_ptr e) noexcept : result_(e) { }

    Internal(const Internal& src) = default;
    Internal(Internal&& src) = default;
    Internal& operator=(const Internal& src) = default;
    Internal& operator=(Internal&& src) = default;

    void Complete(T&& v) noexcept {
      assert(yet());
      result_ = std::move(v);
      Finalize();
    }
    void Throw(std::exception_ptr e = std::current_exception()) noexcept {
      assert(yet());
      assert(nullptr != e);
      result_ = e;
      Finalize();
    }
    void Listen(Listener&& listener) {
      assert(!calling_listener_ &&
             "do not add listener while listener callback");
      if (yet()) {
        try {
          listeners_.push_back(std::move(listener));
        } catch (const std::exception&) {
          std::throw_with_nested(
              Exception {"failed to push a listner to the future"});
        }
      } else {
        calling_listener_ = true;
        listener(*this);
        calling_listener_ = false;
      }
    }

    void Ref() noexcept {
      ++refcnt_;
    }
    void Unref() noexcept {
      if (--refcnt_ == 0 && yet()) {
        Throw(std::make_exception_ptr(Exception {"forgotten"}));
      }
    }

    bool yet() const noexcept { return std::holds_alternative<Yet>(result_); }
    bool done() const noexcept { return std::holds_alternative<T>(result_); }
    std::exception_ptr error() const noexcept {
      return std::holds_alternative<std::exception_ptr>(result_)
          ? std::get<std::exception_ptr>(result_)
          : std::exception_ptr {};
    }

    const T& value() const {
      assert(!yet());
      if (auto err = error()) {
        std::rethrow_exception(err);
      }
      assert(done());
      return std::get<T>(result_);
    }

   private:
    void Finalize() noexcept {
      calling_listener_ = true;
      for (auto& listener : listeners_) {
        listener(*this);
      }
      listeners_.clear();
      calling_listener_ = false;
    }

    struct Yet {};
    std::variant<Yet, T, std::exception_ptr> result_;

    std::vector<Listener> listeners_;

    uint64_t refcnt_ = 0;

    bool calling_listener_ = false;
  };

  Future() = delete;
  Future(T&& v) : internal_(Internal(std::move(v))) {
  }
  Future(std::exception_ptr e) : internal_(Internal(e)) {
  }

  Future(const Future&) = default;
  Future(Future&&) = default;
  Future& operator=(const Future&) = default;
  Future& operator=(Future&&) = default;

  Future<T>& Listen(Listener&& listener) {
    internal().Listen(std::move(listener));
    return *this;
  }
  template <typename V>
  Future<T>& Attach(const std::shared_ptr<V>& ptr) {
    return Listen([ptr](auto&) {});
  }
  Future<T>& Then(std::function<void(const T&)>&& f) {
    Listen([f = std::move(f)](auto& fu) noexcept {
      if (fu.done()) {
        f(fu.value());
      }
    });
    return *this;
  }
  Future<T>& Catch(std::function<void(const std::exception&)>&& f) {
    Listen([f = std::move(f)](auto& fu) noexcept {
      try {
        fu.value();
      } catch (const std::exception& e) {
        f(e);
      }
    });
    return *this;
  }

  template <typename F, typename R = decltype((*(F*)0)(*(T*)0))>
  auto ThenAnd(F&& f) -> std::enable_if_t<FutureLike<R>, R> {
    typename R::Completer comp;
    Listen([comp, f = std::move(f)](auto& fu) mutable {
      try {
        f(fu.value()).Chain(comp);
      } catch (const std::exception&) {
        comp.Throw();
      }
    });
    return comp.future();
  }

  template <typename F, typename R = decltype((*(F*)0)(*(T*)0))>
  auto ThenAnd(F&& f) -> std::enable_if_t<!FutureLike<R>, Future<R>> {
    typename Future<R>::Completer comp;
    Listen([comp, f = std::move(f)](auto& fu) mutable {
      comp.Run([&]() { return f(fu.value()); });
    });
    return comp.future();
  }

  auto Chain(auto& comp)
      -> decltype(comp.Run([&]() { return *(T*)0; }), comp.future()) {
    Listen([comp](auto& fu) mutable {
      comp.Run([&]() { return fu.value(); });
    });
    return comp.future();
  }

  template <typename F>
  auto Chain(auto& comp, F&& f)
      -> decltype(comp.Run([&]() { return (*(F*)0)(*(T*)0); }), comp.future()) {
    Listen([comp, f = std::move(f)](auto& fu) mutable {
      comp.Run([&]() { return f(fu.value()); });
    });
    return comp.future();
  }

 public:
  bool yet() const noexcept { return internal().yet(); }
  bool done() const noexcept { return internal().done(); }
  std::exception_ptr error() const noexcept { return internal().error(); }
  const T& value() const { return internal().value(); }

 private:
  Future(std::shared_ptr<Internal>&& in) noexcept
      : internal_(std::move(in)) { }
  Future(const std::shared_ptr<Internal>& in) noexcept
      : internal_(std::move(in)) { }

  Internal& internal() noexcept {
    return std::holds_alternative<Internal>(internal_)
        ? std::get<Internal>(internal_)
        : *std::get<std::shared_ptr<Internal>>(internal_);
  }
  const Internal& internal() const noexcept {
    return std::holds_alternative<Internal>(internal_)
        ? std::get<Internal>(internal_)
        : *std::get<std::shared_ptr<Internal>>(internal_);
  }

  std::variant<Internal, std::shared_ptr<Internal>> internal_;
};

template <typename T>
class Future<T>::Completer final {
 public:
  Completer()
      : internal_(std::make_shared<Internal>()) {
    internal_->Ref();
  }
  ~Completer() noexcept {
    if (nullptr != internal_) {
      internal_->Unref();
    }
  }

  Completer(const Completer& src) noexcept : internal_(src.internal_) {
    if (nullptr != internal_) {
      internal_->Ref();
    }
  }
  Completer(Completer&&) = default;
  Completer& operator=(const Completer& src) noexcept {
    if (this != &src) {
      if (nullptr != internal_) {
        internal_->Unref();
      }
      internal_ = src.internal_;
      if (nullptr != internal_) {
        internal_->Ref();
      }
    }
    return *this;
  }
  Completer& operator=(Completer&& src) noexcept {
    if (this != &src) {
      if (nullptr != internal_) {
        internal_->Unref();
      }
      internal_ = std::move(src.internal_);
    }
    return *this;
  }

 public:
  template <typename V>
  Completer& Attach(const std::shared_ptr<V>& ptr) {
    internal_->Listen([ptr](auto&) {});
    return *this;
  }
  Completer& Complete(T&& v) noexcept {
    assert(nullptr != internal_);
    internal_->Complete(std::move(v));
    return *this;
  }
  Completer& Throw(std::exception_ptr e = std::current_exception()) noexcept {
    assert(nullptr != internal_);
    internal_->Throw(e);
    return *this;
  }
  Completer& Run(std::function<T()>&& f) noexcept {
    assert(nullptr != internal_);
    try {
      Complete(f());
    } catch (const std::exception&) {
      Throw();
    }
    return *this;
  }
  template <typename AQ, typename SQ, typename F>
  Completer& RunAsync(const std::shared_ptr<AQ>& aq,
                const std::shared_ptr<SQ>& sq,
                F&& f) noexcept {
    assert(nullptr != internal_);
    aq->Exec([*this, sq, f = std::move(f)](auto& ctx) mutable {
      static_assert(std::is_invocable_v<F, decltype(ctx)>);
      try {
        sq->Exec([*this, ret = f(ctx)](auto&) mutable {
          Complete(std::move(ret));
        });
      } catch (const std::exception&) {
        const auto eptr = std::current_exception();
        sq->Exec([*this, eptr](auto&) mutable { Throw(eptr); });
      }
    });
    return *this;
  }

 private:
  template <typename V, FutureLike Fu, typename... Args>
  static void RunAfter_Each(
      const std::shared_ptr<V>& ptr, Fu fu, Args&&... args) {
    fu.Attach(ptr);
    RunAfter_Each(ptr, std::forward<Args>(args)...);
  }
  static void RunAfter_Each(const std::shared_ptr<void>&) noexcept { }

 public:
  template <typename F, typename... Args>
  Completer& RunAfter(F&& f, Args&&... args) {
    struct A final {
     public:
      using Tuple = std::tuple<std::remove_reference_t<Args>...>;

     public:
      A(const Completer& self, F&& f, Tuple&& args) noexcept
          : self_(self), f_(std::move(f)), args_(std::move(args)) { }
      ~A() noexcept {
        self_.Run([this]() { return std::apply(f_, args_); });
      }

     private:
      Completer self_;
      F f_;
      Tuple args_;
    };
    RunAfter_Each(
        std::make_shared<A>(*this, std::move(f), std::make_tuple(args...)),
        std::forward<Args>(args)...);
    return *this;
  }

 public:
  Future<T> future() const noexcept {
    assert(nullptr != internal_);
    return {internal_};
  }

 private:
  std::shared_ptr<Internal> internal_;
};

}  // namespace nf7
