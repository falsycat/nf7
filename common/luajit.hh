#pragma once

#include <lua.hpp>


namespace nf7::luajit {

static inline void PushImmEnv(lua_State* L) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::PushImmEnv")) {
    lua_createtable(L, 0, 0);
      lua_pushvalue(L, LUA_GLOBALSINDEX);
      lua_setfield(L, -2, "__index");

      lua_pushcfunction(L, [](auto L) { return luaL_error(L, "global is immutable"); });
      lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
  }
}

static inline int SandboxCall(lua_State* L, int narg, int nret) noexcept {
  constexpr size_t kSandboxInstructionLimit = 10000000;

  static const auto kHook = [](auto L, auto) {
    luaL_error(L, "reached instruction limit (<=1e7)");
  };
  lua_sethook(L, kHook, LUA_MASKCOUNT, kSandboxInstructionLimit);

  PushImmEnv(L);
  lua_setfenv(L, -narg-2);

  return lua_pcall(L, narg, nret, 0);
}

}  // namespace nf7
