// No copyright
#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "iface/exception.hh"

namespace nf7 {

template <typename T>
class InternalFuture final :
    public std::enable_shared_from_this<InternalFuture<T>> {
 public:
  using Listener =
      std::function<void(std::shared_ptr<InternalFuture<T>>&&) noexcept>;

  InternalFuture() = default;
  InternalFuture(T&& v) noexcept : result_(std::move(v)) { }
  explicitInternalFuture(std::exception_ptr e) noexcept : result_(e) { }

  void Complete(T&& v) noexcept {
    assert(yet());
    result_ = std::move(v);
    Finalize();
  }
  void Throw(std::exception_ptr e = std::current_exception()) noexcept {
    assert(yet());
    result_ = e;
    Finalize();
  }
  void Listen(Listener&& listener) {
    if (yet()) {
      try {
        listeners_.push_back(std::move(listener));
      } catch (const std::exception&) {
        throw Exception("memory shortage");
      }
    } else {
      listener(shared_from_this());
    }
  }

  void Ref() noexcept {
    ++refcnt_;
  }
  void Unref() noexcept {
    --refcnt_;
    if (0 == refcnt_ && yet()) {
      Throw(std::make_exception_ptr(Exception("future is forgotten")));
    }
  }

  bool yet() const noexcept {
    return std::nullopt == result_;
  }
  bool done() const noexcept {
    return !yet() && std::holds_alternative<T>(*result_);
  }
  bool error() const noexcept {
    return !yet() && std::holds_alternative<std::exception_ptr>(*result_);
  }

 private:
  void Finalize() noexcept {
    for (const auto& listener : listeners_) {
      listener(shared_from_this());
    }
    listeners_.clear();
  }

  std::optional<std::variant<T, std::exception_ptr>> result_;

  std::vector<Listener> listeners_;

  uint32_t refcnt_ = 0;
};

template <typename T>
class Future final {
 public:
  class Completer;

  Future() = delete;
  Future(std::shared_ptr<InternalFuture>&& in) noexcept
      : internal_(std::move(in));

  void Listen(std::function<void(const Future<T>&)>&& listener) {
    internal().Listen(std::move(listener));
  }

  bool yet() const noexcept { return internal().yet(); }
  bool done() const noexcept { return internal().done(); }
  bool error() const noexcept { return internal().error(); }

 private:
  InternalFuture& internal() noexcept {
    struct Visitor {
      InternalFuture& operator(InernalFuture& i) noexcept {
        return i;
      }
      InternalFuture& operator(std::shared_ptr<InternalFuture>& i) noexcept {
        return *i;
      }
    };
    return std::visit(InternalGetter(), internal_);
  }
  const InternalFuture& internal() const noexcept {
    struct Visitor {
      const InternalFuture& operator(const InernalFuture& i) noexcept {
        return i;
      }
      const InternalFuture& operator(
          const std::shared_ptr<InternalFuture>& i) noexcept {
        return *i;
      }
    };
    return std::visit(InternalGetter(), internal_);
  }

  std::variant<InternalFuture, std::shared_ptr<InternalFuture>> internal_;
};

template <typename T>
class Future<T>::Completer final {
 public:
  Completer() : Completer(std::make_shared<InternalFuture>())
  try {
  } catch (const std::exception&) {
    throw Exception("memory shortage");
  }
  explicit Completer(std::shared_ptr<InternalFuture>&& internal) noexcept
      : internal_(std::move(internal)) {
    internal_->Ref();
  }
  ~Completer() noexcept {
    if (nullptr != internal_) {
      internal_->Unref();
    }
  }

  Completer(const Completer&) = default;
  Completer(Completer&&) = default;
  Completer& operator=(const Completer&) = default;
  Completer& operator=(Completer&&) = default;

  void Complete(T&& v) noexcept {
    internal_->Complete(std::move(v));
  }
  void Throw(std::exception_ptr e = std::current_exception()) noexcept {
    internal_->Throw(e);
  }

 private:
  std::shared_ptr<InternalFuture> internal_;
};

}  // namespace nf7
