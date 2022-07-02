#include "common/luajit_thread.hh"

#include <sstream>


namespace nf7::luajit {

constexpr const char* kTypeName         = "nf7::luajit::Thread";
constexpr size_t      kInstructionLimit = 10000000;


Thread::~Thread() noexcept {
  if (holder_) *holder_ = nullptr;
}

void Thread::PushMeta(lua_State* L) noexcept {
  if (luaL_newmetatable(L, kTypeName)) {
    PushWeakPtrDeleter<Thread>(L);
    lua_setfield(L, -2, "__gc");

    lua_createtable(L, 0, 0);
    {
      lua_pushcfunction(L, [](auto L) {
        auto th = CheckWeakPtr<Thread>(L, 1, kTypeName);
        th->ExpectYield();
        th->ExecResume(L);
        return lua_yield(L, lua_gettop(L)-1);
      });
      lua_setfield(L, -2, "yield");

      static const auto log_write = [](lua_State* L, nf7::Logger::Level lv) {
        auto th     = CheckWeakPtr<Thread>(L, 1, kTypeName);
        auto logger = th->logger_;
        if (!logger) return luaL_error(L, "logger is not installed on current thread");

        const int n = lua_gettop(L);
        std::stringstream st;
        for (int i = 2; i <= n; ++i) {
          st << lua_tostring(L, i);
        }
        logger->Write({lv, st.str()});
        return 0;
      };
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kTrace); });
      lua_setfield(L, -2, "trace");
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kInfo); });
      lua_setfield(L, -2, "info");
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kWarn); });
      lua_setfield(L, -2, "warn");
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kError); });
      lua_setfield(L, -2, "error");
    }
    lua_setfield(L, -2, "__index");
  }
}

void Thread::Resume(lua_State* L, int narg) noexcept {
  std::unique_lock<std::mutex> k(mtx_);

  if (state_ == kAborted) return;
  assert(holder_);
  assert(L      == th_);
  assert(state_ == kPaused);
  (void) L;

  static const auto kHook = [](auto L, auto) {
    luaL_error(L, "reached instruction limit (<=1e7)");
  };
  lua_sethook(th_, kHook, LUA_MASKCOUNT, kInstructionLimit);

  PushGlobalTable(th_);
  PushWeakPtr(th_, weak_from_this());
  Thread::PushMeta(th_);
  lua_setmetatable(th_, -2);
  lua_setfield(th_, -2, "nf7");
  lua_pop(th_, 1);

  state_ = kRunning;
  k.unlock();
  const auto ret = lua_resume(th_, narg);
  k.lock();
  if (state_ == kAborted) return;
  switch (ret) {
  case 0:
    state_ = kFinished;
    if (holder_) *holder_ = nullptr;
    break;
  case LUA_YIELD:
    state_ = kPaused;
    break;
  default:
    state_ = kAborted;
    if (holder_) *holder_ = nullptr;
  }
  if (!std::exchange(skip_handle_, false)) {
    handler_(*this, th_);
  }
}
void Thread::Abort() noexcept {
  std::unique_lock<std::mutex> k(mtx_);
  state_ = kAborted;
  if (holder_) *holder_ = nullptr;
}


void Thread::Holder::Handle(const nf7::File::Event& ev) noexcept {
  std::unique_lock<std::mutex> k(mtx_);

  switch (ev.type) {
  case nf7::File::Event::kAdd:
    assert(isolated_);
    isolated_ = false;

    if (th_) {
      th_->file_parent_ = owner_;
      if (auto& f = th_->file_) {
        assert(!f->parent());
        f->MoveUnder(*owner_, "file");
      }
    }
    return;
  case nf7::File::Event::kRemove:
    assert(!isolated_);
    isolated_ = true;

    if (th_) {
      th_->file_parent_ = nullptr;
      if (auto& f = th_->file_) {
        assert(f->parent());
        f->Isolate();
      }
    }
    return;
  default:
    return;
  }
}

void Thread::Holder::Assign(const std::shared_ptr<Thread>& th) noexcept {
  if (th_ == th) return;
  if (th_) {
    th_->holder_ = nullptr;
    if (!isolated_) {
      if (auto& f = th_->file_) {
        assert(f->parent());
        f->Isolate();
      }
    }
  }
  th_ = th;
  if (th_) {
    th_->holder_ = this;
    if (!isolated_) {
      if (auto& f = th_->file_) {
        assert(!f->parent());
        f->MoveUnder(*owner_, "file");
      }
    }
  }
}

}  // namespace nf7::luajit