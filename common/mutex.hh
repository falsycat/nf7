#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <utility>

#include "common/life.hh"
#include "common/future.hh"


namespace nf7 {

// nf7::Mutex is not thread-safe except Mutex::Lock's destructor.
class Mutex final {
 public:
  class Sync;
  class Lock;
  template <typename T> class Resource;

  Mutex() noexcept : life_(*this) {
  }

  // It's guaranteed that the promise is finalized in a sub task or is done immediately.
  nf7::Future<std::shared_ptr<Lock>> AcquireLock(
      const std::shared_ptr<nf7::Context>& ctx, bool ex = false) noexcept {
    if (auto ret = TryAcquireLock(ctx, ex)) {
      return {ret};
    } else {
      if (ex || pends_.size() == 0 || pends_.back().ex) {
        pends_.push_back({.pro = {ctx}, .ctx = ctx, .ex = ex});
      }
      return pends_.back().pro.future();
    }
  }
  std::shared_ptr<Lock> TryAcquireLock(
      const std::shared_ptr<nf7::Context>& ctx, bool ex = false) noexcept {
    auto k = TryAcquireLock_(ctx, ex);
    if (k) {
      onLock();
    }
    return k;
  }

  const char* status() const noexcept {
    return sync_.expired()? "free": ex_? "exlocked": "locked";
  }
  size_t pendings() const noexcept {
    return pends_.size();
  }

  std::function<void()> onLock   = [](){};
  std::function<void()> onUnlock = [](){};

 private:
  nf7::Life<Mutex> life_;

  bool ex_ = false;
  std::weak_ptr<Sync> sync_;

  struct Item final {
    nf7::Future<std::shared_ptr<Lock>>::Promise pro;
    std::shared_ptr<nf7::Context> ctx;
    bool ex;
  };
  std::deque<Item> pends_;


  std::shared_ptr<Lock> TryAcquireLock_(
      const std::shared_ptr<nf7::Context>& ctx, bool ex) noexcept {
    auto sync = sync_.lock();
    if (sync) {
      if (ex_ || ex) return nullptr;
    } else {
      sync  = std::make_shared<Sync>(*this);
      ex_   = ex;
      sync_ = sync;
    }
    return std::make_shared<Mutex::Lock>(ctx, sync);
  }
};

class Mutex::Sync {
 public:
  friend nf7::Mutex;

  Sync() = delete;
  Sync(nf7::Mutex& mtx) noexcept : mtx_(mtx.life_) {
  }
  Sync(const Sync&) = delete;
  Sync(Sync&&) = delete;
  Sync& operator=(const Sync&) = delete;
  Sync& operator=(Sync&&) = delete;

  ~Sync() noexcept {
    if (mtx_) {
      auto& pends = mtx_->pends_;
      if (pends.size() > 0) {
        auto item = std::move(pends.front());
        pends.pop_front();
        mtx_->ex_   = false;
        mtx_->sync_ = {};

        auto k = mtx_->TryAcquireLock_(item.ctx, item.ex);
        assert(k);
        item.pro.Return(std::move(k));
      } else {
        mtx_->onUnlock();
      }
    }
  }

 private:
  nf7::Life<nf7::Mutex>::Ref mtx_;
};

class Mutex::Lock {
 public:
  Lock(const std::shared_ptr<nf7::Context>& ctx,
       const std::shared_ptr<Mutex::Sync>&  sync) noexcept :
      ctx_(ctx), sync_(sync) {
  }
  Lock(const Lock&) = default;
  Lock(Lock&&) = default;
  Lock& operator=(const Lock&) = default;
  Lock& operator=(Lock&&) = default;

  ~Lock() noexcept {
    // Ensure that the Sync's destructor is called on worker thread.
    ctx_->env().ExecSub(
        ctx_, [sync = std::move(sync_)]() mutable { sync = nullptr; });
  }

 private:
  std::shared_ptr<nf7::Context> ctx_;
  std::shared_ptr<Mutex::Sync> sync_;
};


template <typename T>
class Mutex::Resource {
 public:
  Resource() = delete;
  Resource(const std::shared_ptr<Mutex::Lock>& k, T&& v) noexcept :
      lock_(k), value_(std::move(v)) {
  }
  Resource(const std::shared_ptr<Mutex::Lock>& k, const T& v) noexcept :
      Resource(k, T {v}) {
  }
  Resource(const Resource&) = default;
  Resource(Resource&&) = default;
  Resource& operator=(const Resource&) = default;
  Resource& operator=(Resource&&) = default;

  T& operator*() noexcept { return value_; }
  const T& operator*() const noexcept { return value_; }

  T* operator->() noexcept { return &value_; }
  const T* operator->() const noexcept { return &value_; }

  const std::shared_ptr<Mutex::Lock>& lock() const noexcept { return lock_; }
  const T& value() const noexcept { return value_; }

 private:
  std::shared_ptr<Mutex::Lock> lock_;
  T value_;
};

}  // namespace nf7
