// No copyright
#include "core/luajit/thread.hh"

#include "core/luajit/context.hh"


namespace nf7::core::luajit {

void Thread::SetUpThread() noexcept {
  luaL_newmetatable(th_, Context::kGlobalTableName);
  {
    new (lua_newuserdata(th_, sizeof(this))) Thread* {this};
    if (luaL_newmetatable(th_, "nf7::Thread")) {
      lua_createtable(th_, 0, 0);
      {
        lua_pushcfunction(th_, [](auto L) {
          luaL_checkudata(L, 1, "nf7::Thread");
          return luaL_error(L, lua_tostring(L, 2));
        });
        lua_setfield(th_, -2, "throw");
      }
      lua_setfield(th_, -2, "__index");
    }
    lua_setmetatable(th_, -2);
    lua_setfield(th_, -2, "nf7");
  }
  lua_pop(th_, 1);
}

}  // namespace nf7::core::luajit
