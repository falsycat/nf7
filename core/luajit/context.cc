// No copyright
#include "core/luajit/context.hh"

#include <atomic>
#include <mutex>
#include <vector>

#include "iface/common/leak_detector.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/parallelism.hh"


namespace nf7::core::luajit {

std::shared_ptr<Value> TaskContext::Register() noexcept {
  const auto index = luaL_ref(state_, LUA_REGISTRYINDEX);
  return std::make_shared<Value>(ctx_, index);
}

void TaskContext::Query(const Value& value) noexcept {
  assert(value.context() == ctx_);
  lua_rawgeti(state_, LUA_REGISTRYINDEX, value.index());
}

void TaskContext::Push(const nf7::Value& v) noexcept {
  NewUserData(v);
  if (luaL_newmetatable(state_, "nf7::Value")) {
    lua_createtable(state_, 0, 0);
    {
      lua_pushcfunction(state_, [](auto L) {
        const nf7::Value& v = CheckUserData<nf7::Value>(L, 1, "nf7::Value");
        lua_pushstring(L,
            v.is<nf7::Value::Null>()   ? "null":
            v.is<nf7::Value::Integer>()? "integer":
            v.is<nf7::Value::Real>()   ? "real":
            v.is<nf7::Value::Buffer>() ? "buffer":
            v.is<nf7::Value::Object>() ? "object":
            v.is<nf7::Value::SharedData>() ? "data":
            "unknown");
        return 1;
      });
      lua_setfield(state_, -2, "type");

      lua_pushcfunction(state_, [](auto L) {
        const nf7::Value& v = CheckUserData<nf7::Value>(L, 1, "nf7::Value");
        try {
          v.data<luajit::Value>()->Push(L);
        } catch (const std::exception&) {
          lua_pushnil(L);
        }
        return 1;
      });
      lua_setfield(state_, -2, "lua");
    }
    lua_setfield(state_, -2, "__index");

    lua_pushcfunction(state_, [](auto L) {
      nf7::Value& v = CheckUserData<nf7::Value>(L, 1, "nf7::Value");
      v.~Value();
      return 0;
    });
    lua_setfield(state_, -2, "__gc");
  }
  lua_setmetatable(state_, -2);
}


namespace {
void SetUpEnv(lua_State* L) noexcept {
  lua_pushthread(L);
  if (luaL_newmetatable(L, "nf7::Context::ImmutableEnv")) {
    lua_createtable(L, 0, 0);
    {
      luaL_newmetatable(L, Context::kGlobalTableName);
      lua_setfield(L, -2, "__index");

      lua_pushcfunction(L, [](auto L) {
        return luaL_error(L, "global is immutable");
      });
      lua_setfield(L, -2, "__newindex");
    }
    lua_setmetatable(L, -2);
  }
  lua_setfenv(L, -2);
  lua_pop(L, 1);
}

class SyncContext final :
    public Context,
    private LeakDetector<SyncContext> {
 public:
  explicit SyncContext(Env& env)
      : Context("nf7::core::luajit::SyncContext", Context::kSync),
        concurrency_(env.Get<subsys::Concurrency>()) {
    SetUpEnv(state());
  }

  void Push(Task&& task) noexcept override {
    ++refcnt_;

    auto self = std::dynamic_pointer_cast<SyncContext>(shared_from_this());
    concurrency_->Push({
      task.after(),
      [this, self, task = std::move(task)](auto&) mutable {
        TaskContext ctx {self, self->state()};
        lua_settop(*ctx, 0);
        task(ctx);
        if (0 == --refcnt_) { lua_gc(*ctx, LUA_GCCOLLECT, 0); }
      },
      task.location()
    });
  }

 protected:
  using Context::shared_from_this;

 private:
  const std::shared_ptr<subsys::Concurrency> concurrency_;
  uint64_t refcnt_ {0};
};

class AsyncContext final :
    public Context,
    private LeakDetector<AsyncContext> {
 public:
  explicit AsyncContext(Env& env)
      : Context("nf7::core::luajit::AsyncContext", Context::kAsync),
        parallelism_(env.Get<subsys::Parallelism>()) {
    SetUpEnv(state());
  }

  void Push(Task&& task) noexcept override {
    ++refcnt_;

    std::unique_lock<std::mutex> k {mtx_};
    const auto first = tasks_.empty();
    tasks_.push_back(std::move(task));
    k.unlock();

    if (first) {
      auto self = std::dynamic_pointer_cast<AsyncContext>(shared_from_this());
      parallelism_->Push({
        task.after(),
        [self](auto&) { self->Consume(); },
        task.location(),
      });
    }
  }

 protected:
  using Context::shared_from_this;

 private:
  void Consume() noexcept {
    std::unique_lock<std::mutex> k {mtx_};
    auto tasks = std::move(tasks_);
    k.unlock();

    auto self = std::dynamic_pointer_cast<AsyncContext>(shared_from_this());
    TaskContext ctx {self, state()};
    for (auto& task : tasks) {
      task(ctx);
      --refcnt_;
    }
    if (0 == refcnt_) { lua_gc(*ctx, LUA_GCCOLLECT, 0); }
  }

 private:
  std::shared_ptr<subsys::Parallelism> parallelism_;

  std::mutex mtx_;
  std::vector<Task> tasks_;

  std::atomic<uint64_t> refcnt_ {0};
};
}  // namespace

std::shared_ptr<Context> Context::Make(Env& env, Kind kind) {
  switch (kind) {
  case kSync:
    return std::make_shared<SyncContext>(env);
  case kAsync:
    return std::make_shared<AsyncContext>(env);
  default:
    assert(false);
  }
}

}  // namespace nf7::core::luajit
