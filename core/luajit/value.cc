// No copyright
#include "core/luajit/value.hh"

#include <utility>

#include "iface/common/exception.hh"
#include "iface/subsys/concurrency.hh"

#include "core/luajit/context.hh"


namespace nf7::core::luajit {

std::shared_ptr<Value> Value::MakeFunction(
    TaskContext& ctx, std::span<const uint8_t> buf, const char* name) {
  const auto ret = luaL_loadbuffer(
      *ctx, reinterpret_cast<const char*>(buf.data()), buf.size(), name);
  switch (ret) {
  case LUA_OK:
    return ctx.Register();
  case LUA_ERRMEM:
    lua_pop(*ctx, 1);
    throw MemoryException {};
  default:
    std::string msg {lua_tostring(*ctx, -1)};
    lua_pop(*ctx, 1);
    throw Exception {"failed to compile a buffer" + msg};
  }
}
Future<std::shared_ptr<Value>> Value::MakeFunction(
    Env& env,
    std::vector<uint8_t>&& v,
    std::string name) noexcept {
  return Future<std::shared_ptr<Value>>::Completer {}
      .RunAsync(
          env.Get<Context>(),
          env.Get<subsys::Concurrency>(),
          [v = std::move(v), name = std::move(name)](auto& ctx) {
            return MakeFunction(ctx, v, name.c_str());
          })
      .future();
}

Value::~Value() noexcept {
  ctx_->Exec([index = index_](auto& ctx) {
    luaL_unref(*ctx, LUA_REGISTRYINDEX, index);
  });
}

}  // namespace nf7::core::luajit
