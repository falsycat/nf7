// No copyright
#pragma once

#include <cassert>
#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "iface/common/exception.hh"

namespace nf7 {

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
          throw Exception("memory shortage");
        }
      } else {
        calling_listener_ = true;
        listener(*this);
        calling_listener_ = false;
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

  void Listen(Listener&& listener) {
    internal().Listen(std::move(listener));
  }

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
  try : internal_(std::make_shared<Internal>()) {
  } catch (const std::exception&) {
    throw Exception("memory shortage");
  }
  ~Completer() noexcept {
    Finalize();
  }

  Completer(const Completer& src) = delete;
  Completer(Completer&&) = default;
  Completer& operator=(const Completer&) = delete;
  Completer& operator=(Completer&& src) noexcept {
    if (this != &src) {
      Finalize();
      internal_ = std::move(src.internal_);
    }
    return *this;
  }

  void Complete(T&& v) noexcept {
    internal_->Complete(std::move(v));
  }
  void Throw(std::exception_ptr e = std::current_exception()) noexcept {
    internal_->Throw(e);
  }
  void Run(std::function<T()>& f) noexcept {
    try {
      Complete(f());
    } catch (...) {
      Throw();
    }
  }

  Future<T> future() const noexcept { return {internal_}; }

 private:
  void Finalize() noexcept {
    if (internal_->yet()) {
      internal_->Throw(std::make_exception_ptr(Exception {"forgotten"}));
    }
  }

  std::shared_ptr<Internal> internal_;
};

}  // namespace nf7
