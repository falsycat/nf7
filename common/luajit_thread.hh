#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <lua.hpp>

#include "nf7.hh"

#include "common/future.hh"
#include "common/lambda.hh"
#include "common/logger_ref.hh"
#include "common/luajit.hh"
#include "common/luajit_ref.hh"


namespace nf7::luajit {

class Thread final : public std::enable_shared_from_this<Thread> {
 public:
  enum State { kInitial, kRunning, kPaused, kFinished, kAborted, };
  using Handler = std::function<void(Thread&, lua_State*)>;

  // Registry keeps an objects used in the Thread and deletes immediately when the Thread ends.
  class RegistryItem;
  class Lambda;
  template <typename T> class Lock;

  class Exception final : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  // Creates a handler to finalize a promise.
  template <typename T>
  static Handler CreatePromiseHandler(
      nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&&) noexcept;

  Thread() = delete;
  Thread(const std::shared_ptr<nf7::Context>&       ctx,
         const std::shared_ptr<nf7::luajit::Queue>& ljq,
         const std::shared_ptr<nf7::Lambda::Owner>& la_owner,
         Handler&& handler) noexcept :
      ctx_(ctx), ljq_(ljq), la_owner_(la_owner), handler_(std::move(handler)) {
  }
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread& operator=(Thread&&) = delete;

  void Install(const std::shared_ptr<nf7::LoggerRef>& logger) noexcept {
    assert(state_ == kInitial);
    logger_ = logger;
  }

  // must be called on luajit thread
  lua_State* Init(lua_State* L) noexcept;

  // must be called on luajit thread
  // L must be a thread state, which is returned by Init().
  void Resume(lua_State* L, int narg) noexcept;

  // must be called on luajit thread
  // handler_ won't be called on next yielding
  void ExpectYield(lua_State*) noexcept {
    skip_handle_ = true;
  }

  // must be called on luajit thread
  void Register(lua_State*, const std::shared_ptr<RegistryItem>& item) noexcept {
    registry_.push_back(item);
  }
  void Forget(lua_State*, const RegistryItem& item) noexcept {
    registry_.erase(
        std::remove_if(registry_.begin(), registry_.end(),
                       [&item](auto& x) { return x.get() == &item; }),
        registry_.end());
  }

  // thread-safe
  void Abort() noexcept;

  // queue a task that exec Resume()
  // thread-safe
  template <typename... Args>
  void ExecResume(lua_State* L, Args&&... args) noexcept {
    auto self = shared_from_this();
    ljq_->Push(ctx_, [this, L, self, args...](auto) mutable {
      Resume(L, luajit::PushAll(L, std::forward<Args>(args)...));
    });
  }

  nf7::Env& env() noexcept { return ctx_->env(); }
  const std::shared_ptr<nf7::Context>& ctx() const noexcept { return ctx_; }
  const std::shared_ptr<nf7::luajit::Queue>& ljq() const noexcept { return ljq_; }
  const std::shared_ptr<nf7::Lambda::Owner>& lambdaOwner() const noexcept { return la_owner_; }
  const std::shared_ptr<nf7::LoggerRef>& logger() const noexcept { return logger_; }
  State state() const noexcept { return state_; }

 private:
  // initialized by constructor
  std::mutex mtx_;

  std::shared_ptr<nf7::Context>       ctx_;
  std::shared_ptr<nf7::luajit::Queue> ljq_;
  std::shared_ptr<nf7::Lambda::Owner> la_owner_;

  Handler handler_;
  std::atomic<State> state_ = kInitial;


  // initialized on Init()
  lua_State* th_ = nullptr;
  std::optional<nf7::luajit::Ref> th_ref_;


  // installed features
  std::shared_ptr<nf7::LoggerRef> logger_;


  // mutable params
  std::vector<std::shared_ptr<RegistryItem>> registry_;

  bool skip_handle_ = false;
};


class Thread::RegistryItem {
 public:
  RegistryItem() = default;
  virtual ~RegistryItem() = default;
  RegistryItem(const RegistryItem&) = delete;
  RegistryItem(RegistryItem&&) = delete;
  RegistryItem& operator=(const RegistryItem&) = delete;
  RegistryItem& operator=(RegistryItem&&) = delete;
};


template <typename T>
Thread::Handler Thread::CreatePromiseHandler(
    nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&& f) noexcept {
  return [&pro, f = std::move(f)](auto& self, auto L) {
    switch (self.state()) {
    case kPaused:
      pro.Throw(std::make_exception_ptr<nf7::Exception>({"unexpected yield"}));
      break;
    case kFinished:
      pro.Wrap([&]() { return f(L); });
      break;
    case kAborted:
      pro.Throw(std::make_exception_ptr<nf7::Exception>({lua_tostring(L, -1)}));
      break;
    default:
      assert(false);
      throw 0;
    }
  };
}

}  // namespace nf7::luajit
