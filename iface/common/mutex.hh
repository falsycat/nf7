// No copyright
#pragma once

#include <memory>

#include "iface/common/future.hh"


namespace nf7 {

class Mutex final {
 private:
  class Impl;

 public:
  class Token;
  using SharedToken = std::shared_ptr<Token>;

 public:
  Mutex();
  ~Mutex() noexcept;

  Mutex(const Mutex&) = delete;
  Mutex(Mutex&&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  Mutex& operator=(Mutex&&) = delete;

 public:
  Future<SharedToken> Lock() noexcept;
  SharedToken TryLock();

  Future<SharedToken> LockEx() noexcept;
  SharedToken TryLockEx();

 private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace nf7
