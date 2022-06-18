#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <lua.hpp>

#include "nf7.hh"

#include "common/future.hh"
#include "common/luajit.hh"
#include "common/luajit_ref.hh"


namespace nf7::luajit {

class Thread final : public std::enable_shared_from_this<Thread> {
 public:
  enum State { kInitial, kRunning, kPaused, kFinished, kAborted, };
  using Handler = std::function<void(Thread&, lua_State*)>;

  class Exception final : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  static std::shared_ptr<Thread> Create(Handler&& handler) noexcept {
    return std::shared_ptr<Thread>{new Thread{std::move(handler)}};
  }
  template <typename T>
  static std::shared_ptr<Thread> CreateForPromise(
      nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&& f) noexcept {
    return std::shared_ptr<Thread>(new Thread{[&pro, f = std::move(f)](auto& self, auto L) {
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
    }});
  }

  static void PushMeta(lua_State*) noexcept;

  Thread() = delete;
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread& operator=(Thread&&) = delete;

  // must be called on luajit thread
  lua_State* Init(const std::shared_ptr<nf7::Context>&       ctx,
                  const std::shared_ptr<nf7::luajit::Queue>& ljq,
                  lua_State* L) noexcept {
    assert(state_ == kInitial);

    ctx_ = ctx;
    ljq_ = ljq;

    th_ = lua_newthread(L);
    PushImmEnv(L);
    lua_setfenv(L, -2);
    th_ref_.emplace(ctx, ljq, luaL_ref(L, LUA_REGISTRYINDEX));

    state_ = kPaused;
    return th_;
  }

  // must be called on luajit thread
  void Resume(lua_State* L, int narg) noexcept;

  void Abort() noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    state_ = kAborted;
  }

  // handler_ won't be called on next yielding
  void ExpectYield() noexcept {
    skip_handle_ = true;
  }

  const std::shared_ptr<nf7::Context>& ctx() const noexcept { return ctx_; }
  const std::shared_ptr<nf7::luajit::Queue>& ljq() const noexcept { return ljq_; }
  State state() const noexcept { return state_; }

 private:
  std::mutex mtx_;

  Handler handler_;
  std::atomic<State> state_ = kInitial;

  std::shared_ptr<nf7::Context>       ctx_;
  std::shared_ptr<nf7::luajit::Queue> ljq_;

  lua_State* th_ = nullptr;
  std::optional<nf7::luajit::Ref> th_ref_;

  bool skip_handle_ = false;


  Thread(Handler&& handler) noexcept : handler_(std::move(handler)) {
  }
};

}  // namespace nf7::luajit
