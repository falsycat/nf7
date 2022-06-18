#pragma once

#include <algorithm>
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
#include "common/proxy_env.hh"


namespace nf7::luajit {

class Thread final : public std::enable_shared_from_this<Thread> {
 public:
  class Holder;

  enum State { kInitial, kRunning, kPaused, kFinished, kAborted, };
  using Handler = std::function<void(Thread&, lua_State*)>;

  class Exception final : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  static void PushMeta(lua_State*) noexcept;

  Thread() = delete;
  Thread(const std::shared_ptr<nf7::Context>&       ctx,
         const std::shared_ptr<nf7::luajit::Queue>& ljq,
         Handler&& handler) noexcept :
      env_(ctx->env()), ctx_(ctx), ljq_(ljq), handler_(std::move(handler)) {
  }
  ~Thread() noexcept;
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread& operator=(Thread&&) = delete;

  // must be called on luajit thread
  lua_State* Init(lua_State* L) noexcept {
    assert(state_ == kInitial);

    th_ = lua_newthread(L);
    PushImmEnv(L);
    lua_setfenv(L, -2);
    th_ref_.emplace(ctx_, ljq_, luaL_ref(L, LUA_REGISTRYINDEX));

    state_ = kPaused;
    return th_;
  }

  // must be called on luajit thread
  void Resume(lua_State* L, int narg) noexcept;

  // queue a task that exec Resume() with narg=0
  void ExecResume(lua_State* L) noexcept {
    ljq_->Push(ctx_, [this, L, self = shared_from_this()](auto) { Resume(L, 0); });
  }

  // thread-safe
  void Abort() noexcept;

  void EmplaceFile(std::string_view name) {
    file_ = nf7::File::registry(name).Create(env_);
    if (file_parent_) {
      file_->MoveUnder(*file_parent_, "file");
    }
  }

  // must be called on luajit thread
  // handler_ won't be called on next yielding
  void ExpectYield() noexcept {
    skip_handle_ = true;
  }

  nf7::Env& env() noexcept { return env_; }
  const std::shared_ptr<nf7::Context>& ctx() const noexcept { return ctx_; }
  const std::shared_ptr<nf7::luajit::Queue>& ljq() const noexcept { return ljq_; }
  State state() const noexcept { return state_; }

 private:
  // initialized by constructor
  std::mutex mtx_;
  nf7::ProxyEnv env_;

  std::shared_ptr<nf7::Context>       ctx_;
  std::shared_ptr<nf7::luajit::Queue> ljq_;

  Handler handler_;
  std::atomic<State> state_ = kInitial;

  // initialized on Init()
  lua_State* th_ = nullptr;
  std::optional<nf7::luajit::Ref> th_ref_;


  // mutable params
  Holder* holder_ = nullptr;

  File* file_parent_ = nullptr;
  std::unique_ptr<nf7::File> file_;

  bool skip_handle_ = false;
};

// Holder handles events for files dynamically created in lua thread
class Thread::Holder final {
 public:
  Holder() = default;
  Holder(File& owner) noexcept : owner_(&owner) {
  }
  ~Holder() noexcept {
    assert(isolated_);
    *this = nullptr;
  }
  Holder(const Holder&) = delete;
  Holder(Holder&&) = delete;
  Holder& operator=(const Holder&) = delete;
  Holder& operator=(Holder&&) = delete;

  // thread-safe
  Holder& operator=(const std::shared_ptr<Thread>& th) noexcept;

  std::shared_ptr<Thread> Emplace(
      const std::shared_ptr<nf7::Context>&       ctx,
      const std::shared_ptr<nf7::luajit::Queue>& ljq,
      Handler&& handler) noexcept {
    *this = std::make_shared<Thread>(ctx, ljq, std::move(handler));
    return th_;
  }
  template <typename T>
  std::shared_ptr<Thread> EmplaceForPromise(
      const std::shared_ptr<nf7::Context>&       ctx,
      const std::shared_ptr<nf7::luajit::Queue>& ljq,
      nf7::Future<T>::Promise& pro, std::function<T(lua_State*)>&& f) noexcept {
    auto handler = [&pro, f = std::move(f)](auto& self, auto L) {
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
    return Emplace(ctx, ljq, std::move(handler));
  }

  void Handle(const nf7::File::Event& ev) noexcept;

  bool holding() const noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    return !!th_;
  }
  nf7::File* child() const noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    return th_? th_->file_.get(): nullptr;
  }

 private:
  mutable std::mutex mtx_;

  nf7::File* const owner_;

  bool isolated_ = true;
  std::shared_ptr<Thread> th_;
};

}  // namespace nf7::luajit
