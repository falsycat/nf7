// No copyright
#pragma once

#include <memory>
#include <utility>

#include "iface/common/future.hh"
#include "iface/common/task_context.hh"


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

 public:
  template <typename F,
            typename R = std::invoke_result_t<F, const SharedToken&>>
  Future<R> RunAsync(const std::shared_ptr<AsyncTaskQueue>& aq,
                     const std::shared_ptr<SyncTaskQueue>&  sq,
                     F&& f,
                     bool ex = false) noexcept {
    typename Future<R>::Completer comp;
    (ex? LockEx(): Lock())
        .Then([aq, sq, f = std::move(f), comp](auto& k) mutable {
          comp.Attach(k);
          comp.RunAsync(aq, sq, [f = std::move(f), k](auto&) mutable {
            return f(k);
          });
        })
        .Catch([comp](auto& e) mutable {
          comp.Throw(std::make_exception_ptr(e));
        });
    return comp.future();
  }
  auto RunAsyncEx(const auto& aq, const auto& sq, auto&& f) noexcept {
    return RunAsync(aq, sq, std::move(f), true);
  }

 private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace nf7
