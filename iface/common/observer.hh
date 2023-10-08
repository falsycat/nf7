// No copyright
// ---- this header provides a way to implement Observer pattern easily
#pragma once

#include <cassert>
#include <optional>
#include <unordered_set>
#include <utility>


namespace nf7 {

// T is notified to Observer<T> by Observer<T>::Target.
// All observers should be based on this.
template <typename T>
class Observer {
 public:
  class Target;
  class Forwarder;

  inline explicit Observer(Target&);
  inline virtual ~Observer() noexcept;

 protected:
  virtual void Notify(const T&) noexcept {}
  virtual void NotifyWithMove(T&& v) noexcept { Notify(v); }
  virtual void NotifyDestruction(const T* = nullptr) noexcept {}

 private:
  Target& target_;
};


// All objects which can be observed by Observer<T> must be based on this.
template <typename T>
class Observer<T>::Target {
 public:
  friend class Observer<T>;

 public:
  Target() = default;
  virtual ~Target() noexcept {
    for (auto obs : obs_) {
      obs->NotifyDestruction();
    }
  }

 public:
  Target(const Target&) = delete;
  Target(Target&&) = delete;
  Target& operator=(const Target&) = delete;
  Target& operator=(Target&&) = delete;

 protected:
  void Notify(const T& v) noexcept {
    assert(!calling_observer_ && "do not call Notify from observer callback");

    calling_observer_ = true;
    for (auto obs : obs_) {
      obs->Notify(v);
    }
    calling_observer_ = false;
  }
  void Notify(T&& v) noexcept {
    assert(!calling_observer_ && "do not call Notify from observer callback");

    if (1 == obs_.size()) {
      auto first = *obs_.begin();
      calling_observer_ = true;
      first->NotifyWithMove(std::move(v));
      calling_observer_ = false;
      return;
    }
    Notify(v);
  }

  bool observed(const T* = nullptr) const noexcept { return !obs_.empty(); }

 private:
  void Register(Observer<T>& obs) {
    assert(!calling_observer_ &&
           "do not register any observer from observer callback");
    obs_.insert(&obs);
  }
  void Unregister(const Observer<T>& obs) noexcept {
    assert(!calling_observer_ &&
           "do not unregister any observer from observer callback");
    obs_.erase(&const_cast<Observer<T>&>(obs));
  }

 private:
  std::unordered_set<Observer<T>*> obs_;

  bool calling_observer_ = false;
};

template <typename T>
class Observer<T>::Forwarder : public Observer<T> {
 public:
  Forwarder(Target& src, Target& dst) noexcept
      : Observer<T>(src), dst_(dst) { }

 public:
  void Notify(const T& v) noexcept override {
    dst_.Notify(v);
  }
  void NotifyWithMove(T&& v) noexcept override {
    dst_.Notify(std::move(v));
  }

 private:
  Target& dst_;
};


template <typename T>
Observer<T>::Observer(Target& target) : target_(target) {
  target_.Register(*this);
}
template <typename T>
Observer<T>::~Observer() noexcept {
  target_.Unregister(*this);
}

}  // namespace nf7
