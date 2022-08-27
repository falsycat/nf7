#include "common/luajit_thread.hh"
#include "common/luajit_thread_lambda.hh"

#include <chrono>
#include <sstream>
#include <tuple>


namespace nf7::luajit {

constexpr size_t kInstructionLimit = 10000000;
constexpr size_t kBufferSizeMax    = 64 * 1024 * 1024;


// Pushes a metatable for Thread object, available on global table as 'nf7'.
static void PushMeta(lua_State*) noexcept;


lua_State* Thread::Init(lua_State* L) noexcept {
  assert(state_ == kInitial);

  th_ = lua_newthread(L);
  PushImmEnv(L);
  lua_setfenv(L, -2);
  th_ref_.emplace(ctx_, ljq_, L);

  state_ = kPaused;
  return th_;
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
  PushMeta(th_);
  lua_setmetatable(th_, -2);
  lua_setfield(th_, -2, "nf7");
  lua_pop(th_, 1);

  state_ = kRunning;
  k.unlock();
  active_ = true;
  const auto ret = lua_resume(th_, narg);
  active_ = false;
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
void Thread::Abort() noexcept {
  std::unique_lock<std::mutex> k(mtx_);
  state_ = kAborted;
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
        th.ExecResume(L);
      } return;
      default:
        if (auto log = th.logger()) {
          log->Warn("invalid use of yield, nf7:yield() or nf7:yield(name, value)");
        }
        th.ExecResume(L);
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


static void PushMeta(lua_State* L) noexcept {
  if (luaL_newmetatable(L, Thread::kTypeName)) {
    PushWeakPtrDeleter<Thread>(L);
    lua_setfield(L, -2, "__gc");

    lua_createtable(L, 0, 0);
    {
      // nf7:resolve(path)
      lua_pushcfunction(L, [](auto L) {
        auto th   = Thread::GetPtr(L, 1);
        auto base = th->ctx()->initiator();

        std::string path = luaL_checkstring(L, 2);
        th->env().ExecSub(th->ctx(), [th, L, base, path = std::move(path)]() {
          try {
            th->ExecResume(L, th->env().GetFileOrThrow(base).ResolveOrThrow(path).id());
          } catch (nf7::File::NotFoundException&) {
            th->ExecResume(L, 0);
          }
        });
        th->ExpectYield(L);
        return lua_yield(L, 0);
      });
      lua_setfield(L, -2, "resolve");

      // nf7:ref(obj)
      lua_pushcfunction(L, [](auto L) {
        auto th = Thread::GetPtr(L, 1);
        lua_pushvalue(L, 2);

        auto ref = std::make_shared<nf7::luajit::Ref>(th->ctx(), th->ljq(), L);
        PushValue(L, nf7::Value {std::move(ref)});
        return 1;
      });
      lua_setfield(L, -2, "ref");

      // nf7:query(file_id, interface)
      lua_pushcfunction(L, [](auto L) {
        auto th = Thread::GetPtr(L, 1);

        const auto  id    = luaL_checkinteger(L, 2);
        std::string iface = luaL_checkstring(L, 3);
        th->env().ExecSub(th->ctx(), [th, L, id, iface = std::move(iface)]() {
          try {
            auto& f = th->env().GetFileOrThrow(static_cast<nf7::File::Id>(id));
            if (iface == "node") {
              Thread::Lambda::CreateAndPush(L, th, f);
            } else {
              throw nf7::Exception {"unknown interface: "+iface};
            }
          } catch (nf7::Exception& e) {
            th->ExecResume(L, nullptr, e.msg());
          }
        });
        th->ExpectYield(L);
        return lua_yield(L, 0);
      });
      lua_setfield(L, -2, "query");

      lua_pushcfunction(L, [](auto L) {
        auto       th  = Thread::GetPtr(L, 1);
        const auto sec = luaL_checknumber(L, 2);

        const auto time = nf7::Env::Clock::now() +
            std::chrono::milliseconds(static_cast<uint64_t>(sec*1000));
        th->ljq()->Push(th->ctx(), [th, L](auto) { th->ExecResume(L); }, time);

        th->ExpectYield(L);
        return lua_yield(L, 0);
      });
      lua_setfield(L, -2, "sleep");

      // nf7:yield(results...)
      lua_pushcfunction(L, [](auto L) {
        return lua_yield(L, lua_gettop(L)-1);
      });
      lua_setfield(L, -2, "yield");

      // logging functions
      static const auto log_write = [](lua_State* L, nf7::Logger::Level lv) {
        auto th     = Thread::GetPtr(L, 1);
        auto logger = th->logger();
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

}  // namespace nf7::luajit
