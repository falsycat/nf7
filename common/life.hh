#pragma once

#include <atomic>
#include <cassert>
#include <memory>

#include "nf7.hh"


namespace nf7 {

template <typename T>
class Life final {
 public:
  class Ref;

  Life() = delete;
  Life(T& target) noexcept : ptr_(&target) {
  }
  ~Life() noexcept {
    if (data_) data_->ptr = nullptr;
  }
  Life(const Life&) = delete;
  Life(Life&&) = delete;
  Life& operator=(const Life&) = delete;
  Life& operator=(Life&&) = delete;

 private:
  T* const ptr_;

  struct Data final {
    std::atomic<T*> ptr;
  };
  std::shared_ptr<Data> data_;
};

template <typename T>
class Life<T>::Ref final {
 public:
  Ref() = default;
  Ref(const Life& life) noexcept {
    if (!life.data_) {
      auto& l = const_cast<Life&>(life);
      l.data_ = std::make_shared<Data>();
      l.data_->ptr = l.ptr_;
    }
    data_ = life.data_;
  }
  Ref(const Ref&) = default;
  Ref(Ref&&) = default;
  Ref& operator=(const Ref&) = default;
  Ref& operator=(Ref&&) = default;

  void EnforceAlive() const {
    if (!data_->ptr) {
      throw nf7::ExpiredException {"target expired"};
    }
  }

  operator bool() const noexcept {
    return !!data_->ptr;
  }
  T& operator*() const noexcept {
    assert(data_->ptr);
    return *data_->ptr;
  }
  T* operator->() const noexcept {
    return &**this;
  }

 private:
  std::shared_ptr<Data> data_;
};


}  // namespace nf7
