#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <utility>

#include "common/future.hh"


namespace nf7 {

// nf7::Mutex is not thread-safe.
class Mutex final {
 public:
  class Lock;

  Mutex() noexcept {
  }
  ~Mutex() noexcept;

  nf7::Future<std::shared_ptr<Lock>> AcquireLock(bool ex = false) noexcept {
    if (auto ret = TryAcquireLock(ex)) {
      return {ret};
    } else {
      if (ex || pends_.size() == 0 || pends_.back().ex) {
        pends_.push_back({.pro = {}, .ex = ex});
      }
      return pends_.back().pro.future();
    }
  }
  std::shared_ptr<Lock> TryAcquireLock(bool ex = false) noexcept {
    auto k = TryAcquireLock_(ex);
    if (k) {
      onLock();
    }
    return k;
  }

  std::function<void()> onLock   = [](){};
  std::function<void()> onUnlock = [](){};

 private:
  bool ex_ = false;
  std::weak_ptr<Lock> k_;

  struct Item final {
    nf7::Future<std::shared_ptr<Lock>>::Promise pro;
    bool ex;
  };
  std::deque<Item> pends_;


  std::shared_ptr<Lock> TryAcquireLock_(bool ex) noexcept {
    if (auto k = k_.lock()) {
      if (!ex_ && !ex) {
        return k;
      }
    } else {
      k   = std::make_shared<Lock>(*this);
      ex_ = ex;
      k_  = k;
      return k;
    }
    return nullptr;
  }
};

class Mutex::Lock final {
 public:
  friend nf7::Mutex;

  Lock() = delete;
  Lock(nf7::Mutex& mtx) noexcept : mtx_(&mtx) {
  }
  Lock(const Lock&) = delete;
  Lock(Lock&&) = delete;
  Lock& operator=(const Lock&) = delete;
  Lock& operator=(Lock&&) = delete;

  ~Lock() noexcept {
    if (mtx_) {
      auto& pends = mtx_->pends_;
      if (pends.size() > 0) {
        auto item = std::move(pends.front());
        pends.pop_front();
        mtx_->ex_ = false;
        mtx_->k_  = {};

        auto k = mtx_->TryAcquireLock_(item.ex);
        assert(k);
        item.pro.Return(std::move(k));
      } else {
        mtx_->onUnlock();
      }
    }
  }

 private:
  nf7::Mutex* mtx_;
};
Mutex::~Mutex() noexcept {
  pends_.clear();
  if (auto k = k_.lock()) {
    k->mtx_ = nullptr;
  }
}

}  // namespace nf7
