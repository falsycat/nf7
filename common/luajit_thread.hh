#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <lua.hpp>

#include "nf7.hh"

#include "common/future.hh"
#include "common/logger_ref.hh"
#include "common/luajit.hh"
#include "common/luajit_ref.hh"
#include "common/node.hh"


namespace nf7::luajit {

class Thread final : public std::enable_shared_from_this<Thread> {
 public:
  static constexpr const char* kTypeName = "nf7::luajit::Thread";

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
  static inline Handler CreatePromiseHandler(
      nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&&) noexcept;

  // Creates a handler to emit yielded value to Node::Lambda.
  static inline Handler CreateNodeLambdaHandler(
      const std::shared_ptr<nf7::Node::Lambda>& caller,
      const std::shared_ptr<nf7::Node::Lambda>& callee) noexcept;

  // must be called on luajit thread
  static std::shared_ptr<Thread> GetPtr(lua_State* L, int idx) {
    auto th = CheckWeakPtr<Thread>(L, idx, kTypeName);
    th->EnsureActive(L);
    return th;
  }

  Thread() = delete;
  Thread(const std::shared_ptr<nf7::Context>&       ctx,
         const std::shared_ptr<nf7::luajit::Queue>& ljq,
         Handler&& handler) noexcept :
      ctx_(ctx), ljq_(ljq), handler_(std::move(handler)) {
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
  void EnsureActive(lua_State* L) {
    if (!active_) {
      luaL_error(L, "thread is not active");
    }
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
  const std::shared_ptr<nf7::LoggerRef>& logger() const noexcept { return logger_; }
  State state() const noexcept { return state_; }

 private:
  // initialized by constructor
  std::mutex mtx_;

  std::shared_ptr<nf7::Context>       ctx_;
  std::shared_ptr<nf7::luajit::Queue> ljq_;

  Handler handler_;
  std::atomic<State> state_ = kInitial;


  // initialized on Init()
  lua_State* th_ = nullptr;
  std::optional<nf7::luajit::Ref> th_ref_;


  // installed features
  std::shared_ptr<nf7::LoggerRef> logger_;


  // mutable params
  std::vector<std::shared_ptr<RegistryItem>> registry_;

  bool active_      = false;  // true while executing lua_resume
  bool skip_handle_ = false;  // handler_ won't be called on next yield
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

Thread::Handler Thread::CreateNodeLambdaHandler(
    const std::shared_ptr<nf7::Node::Lambda>& caller,
    const std::shared_ptr<nf7::Node::Lambda>& callee) noexcept {
  return [caller, callee](auto& th, auto L) {
    switch (th.state()) {
    case nf7::luajit::Thread::kPaused:
      switch (lua_gettop(L)) {
      case 0:
        th.ExecResume(L);
        return;
      case 2: {
        auto k = luaL_checkstring(L, 1);
        auto v = nf7::luajit::CheckValue(L, 2);
        caller->env().ExecSub(
            caller, [caller, callee, k = std::string {k}, v = std::move(v)]() {
              caller->Handle(k, v, callee);
            });
      } return;
      default:
        if (auto log = th.logger()) {
          log->Warn("invalid use of yield, nf7:yield() or nf7:yield(name, value)");
        }
        th.Resume(L, 0);
        return;
      }

    case nf7::luajit::Thread::kFinished:
      return;

    default:
      if (auto log = th.logger()) {
        log->Warn(std::string {"luajit execution error: "}+lua_tostring(L, -1));
      }
      return;
    }
  };
}

}  // namespace nf7::luajit
