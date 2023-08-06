// No copyright
#include "core/luajit/context.hh"

#include "iface/common/leak_detector.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/parallelism.hh"


namespace nf7::core::luajit {

Value::~Value() noexcept {
  ctx_->Push(Task {[index = index_](auto& ctx) {
    luaL_unref(*ctx, LUA_REGISTRYINDEX, index);
  }});
}


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
            "unknown");
        return 1;
      });
      lua_setfield(state_, -2, "type");

      // TODO(falsycat)
    }
    lua_setfield(state_, -2, "__index");
  }
  lua_setmetatable(state_, -2);
}


namespace {
template <typename T>
class ContextImpl final :
    public Context,
    private LeakDetector<ContextImpl<T>> {
 public:
  ContextImpl(const char* name, Kind kind, Env& env)
      : Context(name, kind), tasq_(env.Get<T>()) {
    auto L = state();

    lua_pushthread(L);
    if (luaL_newmetatable(L, "nf7::Context::ImmutableEnv")) {
      lua_createtable(L, 0, 0);
      {
        luaL_newmetatable(L, kGlobalTableName);
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

  void Push(Task&& task) noexcept override {
    auto self = std::dynamic_pointer_cast<ContextImpl<T>>(shared_from_this());
    tasq_->Push(typename T::Item {
      [self, task = std::move(task)](auto&) mutable {
        TaskContext ctx {self, self->state()};
        lua_settop(*ctx, 0);
        task(ctx);
      },
      task.location()
    });
  }

 protected:
  using Context::shared_from_this;

 private:
  std::shared_ptr<T> tasq_;
};
}  // namespace

std::shared_ptr<Context> Context::Create(Env& env, Kind kind) {
  switch (kind) {
  case kSync:
    return std::make_shared<ContextImpl<subsys::Concurrency>>(
        "nf7::core::luajit::SyncContext", kSync, env);
  case kAsync:
    return std::make_shared<ContextImpl<subsys::Parallelism>>(
        "nf7::core::luajit::AsyncContext", kAsync, env);
  default:
    assert(false);
  }
}

}  // namespace nf7::core::luajit
