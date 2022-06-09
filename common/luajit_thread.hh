#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <lua.hpp>

#include "nf7.hh"

#include "common/luajit.hh"
#include "common/luajit_ref.hh"


namespace nf7::luajit {

class Thread final : public std::enable_shared_from_this<Thread> {
 public:
  static constexpr size_t      kInstructionLimit = 10000000;
  static constexpr const char* kInstanceName     = "nf7::luajit::Thread::instance_";

  enum State { kInitial, kRunning, kPaused, kFinished, kAborted, };
  using Handler = std::function<void(Thread&, lua_State*)>;

  class Exception final : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  template <typename T>
  static Thread CreateForPromise(nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&& f) noexcept {
    return Thread([&pro, f = std::move(f)](auto& self, auto L) {
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
    });
  }

  Thread() = delete;
  Thread(Handler&& handler) noexcept : handler_(std::move(handler)) {
  }
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread& operator=(Thread&&) = delete;

  // must be called on luajit thread
  // be carefully on recursive reference, ctx is held until *this is destructed.
  lua_State* Init(const std::shared_ptr<nf7::Context>& ctx,
                  const std::shared_ptr<nf7::luajit::Queue>& q,
                  lua_State* L) noexcept {
    assert(state_ == kInitial);

    th_ = lua_newthread(L);
    PushImmEnv(L);
    lua_setfenv(L, -2);
    th_ref_.emplace(ctx, q, luaL_ref(L, LUA_REGISTRYINDEX));

    state_ = kPaused;
    return th_;
  }

  // must be called on luajit thread
  void Resume(lua_State* L, int narg) noexcept {
    std::unique_lock<std::mutex> k(mtx_);

    if (state_ == kAborted) return;
    assert(L      == th_);
    assert(state_ == kPaused);
    (void) L;

    static const auto kHook = [](auto L, auto) {
      luaL_error(L, "reached instruction limit (<=1e7)");
    };
    lua_sethook(th_, kHook, LUA_MASKCOUNT, kInstructionLimit);

    // TODO: push weak_ptr instead
    lua_pushstring(th_, kInstanceName);
    lua_pushlightuserdata(th_, this);
    lua_rawset(th_, LUA_REGISTRYINDEX);

    state_ = kRunning;
    k.unlock();
    const auto ret = lua_resume(th_, narg);
    k.lock();
    if (state_ == kAborted) return;
    switch (ret) {
    case 0:
      state_ = kFinished;
      break;
    case LUA_YIELD:
      state_ = kPaused;
      break;
    default:
      state_ = kAborted;
    }
    if (!std::exchange(skip_handle_, false)) {
      handler_(*this, th_);
    }
  }

  void Abort() noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    state_ = kAborted;
  }

  // handler_ won't be called on next yielding
  void ExpectYield() noexcept {
    skip_handle_ = true;
  }

  State state() const noexcept { return state_; }

 private:
  std::mutex mtx_;

  Handler handler_;
  std::atomic<State> state_ = kInitial;

  lua_State* th_ = nullptr;
  std::optional<nf7::luajit::Ref> th_ref_;

  bool skip_handle_ = false;
};

}  // namespace nf7::luajit
