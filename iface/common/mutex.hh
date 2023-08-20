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

  Mutex(const Mutex&) = default;
  Mutex(Mutex&&) = default;
  Mutex& operator=(const Mutex&) = default;
  Mutex& operator=(Mutex&&) = default;

 public:
  Future<SharedToken> Lock() noexcept;
  SharedToken TryLock();

  Future<SharedToken> LockEx() noexcept;
  SharedToken TryLockEx();

 private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace nf7
