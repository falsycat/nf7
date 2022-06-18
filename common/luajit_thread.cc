#include "common/luajit_thread.hh"


namespace nf7::luajit {

constexpr size_t kInstructionLimit = 10000000;


void Thread::PushMeta(lua_State* L) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::Thread")) {
    PushWeakPtrDeleter<Thread>(L);
    lua_setfield(L, -2, "__gc");

    lua_createtable(L, 0, 0);
    {
      lua_pushcfunction(L, [](auto L) {
        auto th = ToSharedPtr<Thread>(L, 1);
        th->ExpectYield();
        th->ljq()->Push(th->ctx(), [th, L](auto) { th->Resume(L, 0); });
        return lua_yield(L, lua_gettop(L)-1);
      });
      lua_setfield(L, -2, "yield");
    }
    lua_setfield(L, -2, "__index");
  }
}

void Thread::Resume(lua_State* L, int narg) noexcept {
  std::unique_lock<std::mutex> k(mtx_);

  if (state_ == kAborted) return;
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

}  // namespace nf7::luajit
