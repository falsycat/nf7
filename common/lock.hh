#pragma once

#include <cassert>
#include <coroutine>
#include <deque>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "nf7.hh"

#include "common/future.hh"


namespace nf7 {

class Lock final {
 public:
  class Resource;
  class Exception : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  Lock() = default;
  Lock(Resource& res, bool ex) noexcept : res_(&res), ex_(ex) {
  }
  inline ~Lock() noexcept;
  Lock(const Lock&) = delete;
  Lock(Lock&&) = delete;
  Lock& operator=(const Lock&) = delete;
  Lock& operator=(Lock&&) = delete;

  void Validate() const {
    if (!res_) throw Lock::Exception("target expired");
  }

 private:
  Resource* res_ = nullptr;
  bool      ex_  = false;
};

class Lock::Resource {
 public:
  friend Lock;

  Resource() = default;
  virtual ~Resource() noexcept {
    if (auto lock = lock_.lock()) {
      lock->res_ = nullptr;
    }
    for (auto pend : pends_) {
      pend.pro.Throw(std::make_exception_ptr<Lock::Exception>({"lock cancelled"}));
    }
  }
  Resource(const Resource&) = delete;
  Resource(Resource&&) = delete;
  Resource& operator=(const Resource&) = delete;
  Resource& operator=(Resource&&) = delete;

  nf7::Future<std::shared_ptr<Lock>> AcquireLock(bool ex) noexcept {
    if (auto ret = TryAcquireLock(ex)) return ret;

    if (ex || pends_.empty() || pends_.back().ex) {
      pends_.push_back(ex);
    }
    return pends_.back().pro.future();
  }
  std::shared_ptr<Lock> TryAcquireLock(bool ex) noexcept {
    if (auto k = lock_.lock()) {
      return !ex && !k->ex_ && pends_.empty()? k: nullptr;
    }
    auto k = std::make_shared<Lock>(*this, ex);
    lock_ = k;
    OnLock();
    return k;
  }

 protected:
  virtual void OnLock() noexcept { }
  virtual void OnUnlock() noexcept { }

 private:
  struct Pending final {
   public:
    Pending(bool ex_) noexcept : ex(ex_) { }

    bool ex;
    nf7::Future<std::shared_ptr<Lock>>::Promise pro;
  };
  std::weak_ptr<Lock> lock_;
  std::deque<Pending> pends_;
};


Lock::~Lock() noexcept {
  if (!res_) return;
  if (res_->pends_.empty()) {
    res_->OnUnlock();
    return;
  }

  auto next = std::move(res_->pends_.front());
  res_->pends_.pop_front();

  auto lock = std::make_shared<Lock>(*res_, next.ex);
  res_->lock_ = lock;
  next.pro.Return(std::move(lock));
}

}  // namespace nf7
