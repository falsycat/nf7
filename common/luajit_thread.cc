#include "common/luajit_thread.hh"

#include <chrono>
#include <sstream>
#include <tuple>
#include <unordered_set>

#include <tracy/Tracy.hpp>

#include "common/node.hh"
#include "common/node_root_lambda.hh"


namespace nf7::luajit {

constexpr size_t kInstructionLimit = 100000;


lua_State* Thread::Init(lua_State* L) noexcept {
  assert(state_ == kInitial);

  th_ = lua_newthread(L);
  th_ref_.emplace(shared_from_this(), ljq_, L);

  state_ = kPaused;
  return th_;
}
void Thread::Resume(lua_State* L, int narg) noexcept {
  std::unique_lock<std::mutex> k(mtx_);

  if (state_ == kAborted) return;
  assert(L      == th_);
  assert(state_ == kPaused);

  // set global table
  Push(L, GlobalTable {});
  Push(L, weak_from_this());
  lua_setfield(L, -2, "nf7");
  lua_pop(L, 1);

  state_  = kRunning;
  active_ = true;
  yield_ctx_.reset();

  k.unlock();
  int ret;
  {
    ZoneScopedN("lua_resume");
    ret = lua_resume(L, narg);
  }
  k.lock();

  active_ = false;
  if (state_ == kAborted) return;
  switch (ret) {
  case 0:
    th_ref_ = std::nullopt;
    state_  = kFinished;
    break;
  case LUA_YIELD:
    state_ = kPaused;
    break;
  default:
    th_ref_ = std::nullopt;
    state_  = kAborted;
  }
  if (!std::exchange(skip_handler_, false)) {
    k.unlock();
    handler_(*this, L);
  }
}
void Thread::Abort() noexcept {
  std::unique_lock<std::mutex> k(mtx_);
  state_  = kAborted;
  th_ref_ = std::nullopt;

  auto wctx = std::move(yield_ctx_);
  yield_ctx_.reset();
  k.unlock();
  if (auto ctx = wctx.lock()) {
    if (ctx.get() != this) {
      ctx->Abort();
    }
  }
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
      case 2:
        if (auto v = nf7::luajit::Peek<nf7::Value>(L, 2)) {
          auto k = luaL_checkstring(L, 1);
          caller->env().ExecSub(
              caller, [caller, callee, k = std::string {k}, v = std::move(v)]() {
                caller->Handle(k, *v, callee);
              });
          th.ExecResume(L);
          return;
        } else {
        }
        /* FALLTHROUGH */
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


template <> void PushMeta<std::weak_ptr<Thread>>(lua_State* L) noexcept {
  if (luaL_newmetatable(L, MetaName<std::weak_ptr<Thread>>::kValue)) {
    lua_pushcfunction(L, [](auto L) {
      Check<std::weak_ptr<Thread>>(L, 1).~weak_ptr();
      return 0;
    });
    lua_setfield(L, -2, "__gc");

    lua_createtable(L, 0, 0);
    {
      // nf7:import(npath)
      lua_pushcfunction(L, [](auto L) {
        auto th = Thread::GetPtr(L, 1);
        auto im = th->importer();
        if (!im) {
          return luaL_error(L, "import is not available in the current thread");
        }
        if (const auto name = lua_tostring(L, 2)) {
          auto fu = im->Import(th, name);
          fu.ThenIf([L, th](auto& obj) {
            th->ExecResume(L, obj);
          }).template Catch<nf7::Exception>([L, th](auto&) {
            if (auto log = th->logger()) {
              log->Warn("import failed, returning nil");
            }
            th->ExecResume(L);
          });
          return th->Yield(L);
        } else {
          return luaL_error(L, "path should be a string");
        }
      });
      lua_setfield(L, -2, "import");

      // nf7:resolve(path)
      lua_pushcfunction(L, [](auto L) {
        auto th   = Thread::GetPtr(L, 1);
        auto base = th->initiator();

        std::string path = luaL_checkstring(L, 2);
        th->env().ExecSub(th, [th, L, base, path = std::move(path)]() {
          try {
            th->ExecResume(L, th->env().GetFileOrThrow(base).ResolveOrThrow(path).id());
          } catch (nf7::File::NotFoundException&) {
            th->ExecResume(L, 0);
          }
        });
        return th->Yield(L);
      });
      lua_setfield(L, -2, "resolve");

      // nf7:query(file_id, interface)
      lua_pushcfunction(L, [](auto L) {
        auto th = Thread::GetPtr(L, 1);

        const auto  id    = luaL_checkinteger(L, 2);
        std::string iface = luaL_checkstring(L, 3);
        th->env().ExecSub(th, [th, L, id, iface = std::move(iface)]() {
          try {
            auto& f = th->env().GetFileOrThrow(static_cast<nf7::File::Id>(id));
            if (iface == "node") {
              th->ExecResume(
                  L, nf7::NodeRootLambda::Create(
                      th, f.template interfaceOrThrow<nf7::Node>()));
            } else {
              throw nf7::Exception {"unknown interface: "+iface};
            }
          } catch (nf7::Exception& e) {
            th->ExecResume(L, nullptr, e.msg());
          }
        });
        return th->Yield(L);
      });
      lua_setfield(L, -2, "query");

      // nf7:sleep(sec)
      lua_pushcfunction(L, [](auto L) {
        auto       th  = Thread::GetPtr(L, 1);
        const auto sec = luaL_checknumber(L, 2);

        const auto time = nf7::Env::Clock::now() +
            std::chrono::milliseconds(static_cast<uint64_t>(sec*1000));
        th->ljq()->Push(th, [th, L](auto) { th->ExecResume(L); }, time);

        return th->Yield(L);
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
          if (auto msg = lua_tostring(L, i)) {
            st << msg;
          } else {
            return luaL_error(L, "cannot stringify %s", luaL_typename(L, i));
          }
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
