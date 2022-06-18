#include "common/luajit.hh"

#include <lua.hpp>


namespace nf7::luajit {

void PushGlobalTable(lua_State* L) noexcept {
  luaL_newmetatable(L, "nf7::luajit::PushGlobalTable");
}
void PushImmEnv(lua_State* L) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::PushImmEnv")) {
    lua_createtable(L, 0, 0);
      PushGlobalTable(L);
      lua_setfield(L, -2, "__index");

      lua_pushcfunction(L, [](auto L) { return luaL_error(L, "global is immutable"); });
      lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
  }
}

}  // namespace nf7::luajit
