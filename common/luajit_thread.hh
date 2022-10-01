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

  class Importer;

  class Exception final : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  // Creates a handler that finalizes a promise.
  template <typename T>
  static inline Handler CreatePromiseHandler(
      nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&&) noexcept;

  // Creates a handler that emits yielded value to Node::Lambda.
  static Handler CreateNodeLambdaHandler(
      const std::shared_ptr<nf7::Node::Lambda>& caller,
      const std::shared_ptr<nf7::Node::Lambda>& callee) noexcept;

  // must be called on luajit thread
  static std::shared_ptr<Thread> GetPtr(lua_State* L, int idx) {
    auto th = CheckRef<std::weak_ptr<Thread>>(L, idx, kTypeName).lock();
    if (th) {
      th->EnsureActive(L);
      return th;
    } else {
      luaL_error(L, "thread expired");
      return nullptr;
    }
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
  void Install(const std::shared_ptr<Importer>& importer) noexcept {
    assert(state_ == kInitial);
    importer_ = importer;
  }
  void Install(const Thread& th) noexcept {
    assert(state_ == kInitial);
    logger_   = th.logger_;
    importer_ = th.importer_;
  }

  // must be called on luajit thread
  lua_State* Init(lua_State* L) noexcept;

  // must be called on luajit thread
  // L must be a thread state, which is returned by Init().
  void Resume(lua_State* L, int narg) noexcept;

  // must be called on luajit thread
  // handler_ won't be called on this yielding
  int Yield(lua_State* L) {
    skip_handle_ = true;
    return lua_yield(L, 0);
  }

  // must be called on luajit thread
  void EnsureActive(lua_State* L) {
    if (!active_) {
      luaL_error(L, "thread is not active");
    }
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

  nf7::Env& env() const noexcept { return ctx_->env(); }
  const std::shared_ptr<nf7::Context>& ctx() const noexcept { return ctx_; }
  const std::shared_ptr<nf7::luajit::Queue>& ljq() const noexcept { return ljq_; }
  const std::shared_ptr<nf7::LoggerRef>& logger() const noexcept { return logger_; }
  const std::shared_ptr<Importer>& importer() const noexcept { return importer_; }
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
  std::shared_ptr<Importer>       importer_;


  // mutable params
  bool active_      = false;  // true while executing lua_resume
  bool skip_handle_ = false;  // handler_ won't be called on next yield
};


class Thread::Importer {
 public:
  Importer() = default;
  virtual ~Importer() = default;
  Importer(const Importer&) = delete;
  Importer(Importer&&) = delete;
  Importer& operator=(const Importer&) = delete;
  Importer& operator=(Importer&&) = delete;

  // be called on luajit thread
  virtual nf7::Future<std::shared_ptr<luajit::Ref>> Import(
      const luajit::Thread&, std::string_view) noexcept = 0;
};


template <typename T>
Thread::Handler Thread::CreatePromiseHandler(
    nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&& f) noexcept {
  return [pro = pro, f = std::move(f)](auto& self, auto L) mutable {
    switch (self.state()) {
    case kPaused:
      pro.template Throw<nf7::Exception>("unexpected yield");
      break;
    case kFinished:
      pro.Wrap([&]() { return f(L); });
      break;
    case kAborted:
      pro.template Throw<nf7::Exception>(lua_tostring(L, -1));
      break;
    default:
      assert(false);
      throw 0;
    }
  };
}

}  // namespace nf7::luajit
