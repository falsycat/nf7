// No copyright
#include "core/luajit/context.hh"

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

void TaskContext::Query(const std::shared_ptr<Value>& value) noexcept {
  assert(nullptr != value);
  assert(value->context() == ctx_);
  lua_rawgeti(state_, LUA_REGISTRYINDEX, value->index());
}


template <typename T>
class ContextImpl final : public Context {
 public:
  ContextImpl(const char* name, Kind kind, Env& env)
      : Context(name, kind), tasq_(env.Get<T>()) { }

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
