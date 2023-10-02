// No copyright
#include "core/luajit/value.hh"

#include "core/luajit/context.hh"


namespace nf7::core::luajit {

Value::~Value() noexcept {
  ctx_->Push(Task {[index = index_](auto& ctx) {
    luaL_unref(*ctx, LUA_REGISTRYINDEX, index);
  }});
}

}  // namespace nf7::core::luajit
