#pragma once

#include <algorithm>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "nf7.hh"


namespace nf7 {

class Lock {
 public:
  class Resource;

  Lock() = default;
  Lock(Resource& res, bool ex) noexcept : res_(&res), ex_(ex) {
  }
  inline ~Lock() noexcept;
  Lock(const Lock&) = delete;
  Lock(Lock&&) = delete;
  Lock& operator=(const Lock&) = delete;
  Lock& operator=(Lock&&) = delete;

  bool cancelled() const noexcept { return !res_; }
  bool acquired() const noexcept { return acquired_; }

 private:
  Resource* res_ = nullptr;
  bool      ex_  = false;
  bool acquired_ = false;
};

class Lock::Resource {
 public:
  friend Lock;

  Resource() = default;
  virtual ~Resource() noexcept {
    if (auto lock = lock_.lock()) {
      lock->res_ = nullptr;
    }
    for (auto lock : plocks_) {
      lock->res_ = nullptr;
    }
  }
  Resource(const Resource&) = delete;
  Resource(Resource&&) = delete;
  Resource& operator=(const Resource&) = delete;
  Resource& operator=(Resource&&) = delete;

  std::shared_ptr<Lock> Acquire(bool ex) noexcept {
    if (auto ret = TryAcquire(ex)) return ret;

    if (!ex && !plocks_.empty() && !plocks_.back()->ex_) {
      return plocks_.back();
    }
    plocks_.push_back(std::make_shared<Lock>(*this, ex));
    return plocks_.back();
  }
  std::shared_ptr<Lock> TryAcquire(bool ex) noexcept {
    if (!lock_.expired()) return nullptr;

    auto ret = std::make_shared<Lock>(*this, ex);
    ret->acquired_ = true;
    lock_ = ret;
    OnLock();
    return ret;
  }

 protected:
  virtual void OnLock() noexcept { }
  virtual void OnUnlock() noexcept { }

 private:
  std::weak_ptr<Lock> lock_;
  std::deque<std::shared_ptr<Lock>> plocks_;
};

Lock::~Lock() noexcept {
  if (!res_) return;

  if (res_->plocks_.empty()) {
    res_->OnUnlock();
    return;
  }
  auto next = std::move(res_->plocks_.front());
  res_->plocks_.pop_front();

  res_->lock_     = next;
  next->acquired_ = true;
}

}  // namespace nf7
