#include <lua.hpp>

#include "common/luajit.hh"


namespace nf7::luajit {

inline void PushStdTable(lua_State* L) noexcept {
  lua_newuserdata(L, 0);
  lua_createtable(L, 0, 0);
  lua_createtable(L, 0, 0);
  {
    // ---- lua lib ----

    // assert(expr[, msg])
    lua_pushcfunction(L, [](auto L) {
      if (lua_toboolean(L, 1)) {
        return 0;
      }
      if (lua_gettop(L) >= 2) {
        return luaL_error(L, lua_tostring(L, 2));
      } else {
        return luaL_error(L, "assertion failure");
      }
    });
    lua_setfield(L, -2, "assert");

    // error(msg)
    lua_pushcfunction(L, [](auto L) {
      return luaL_error(L, luaL_checkstring(L, 1));
    });
    lua_setfield(L, -2, "error");

    // load(str)
    lua_pushcfunction(L, [](auto L) {
      if (0 != luaL_loadstring(L, luaL_checkstring(L, 1))) {
        return luaL_error(L, "lua.load error: %s", lua_tostring(L, -1));
      }
      return 1;
    });
    lua_setfield(L, -2, "load");

    // pcall(func, args...) -> success, result
    lua_pushcfunction(L, [](auto L) {
      if (0 == lua_pcall(L, lua_gettop(L)-1, LUA_MULTRET, 0)) {
        lua_pushboolean(L, true);
        lua_insert(L, 1);
        return lua_gettop(L);
      } else {
        lua_pushboolean(L, false);
        lua_insert(L, 1);
        return 2;
      }
    });
    lua_setfield(L, -2, "pcall");


    // ---- math lib ----

    // sin(theta)
    lua_pushcfunction(L, [](auto L) {
      lua_pushnumber(L, std::sin(luaL_checknumber(L, 1)));
      return 1;
    });
    lua_setfield(L, -2, "sin");

    // cos(theta)
    lua_pushcfunction(L, [](auto L) {
      lua_pushnumber(L, std::cos(luaL_checknumber(L, 1)));
      return 1;
    });
    lua_setfield(L, -2, "cos");

    // tan(theta)
    lua_pushcfunction(L, [](auto L) {
      lua_pushnumber(L, std::tan(luaL_checknumber(L, 1)));
      return 1;
    });
    lua_setfield(L, -2, "tan");


    // ---- table lib ----

    // meta(table, meta_table)
    lua_pushcfunction(L, [](auto L) {
      luaL_checktype(L, 1, LUA_TTABLE);
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_settop(L, 2);
      lua_setmetatable(L, 1);
      return 1;
    });
    lua_setfield(L, -2, "meta");


    // ---- time lib ----

    // now()
    lua_pushcfunction(L, [](auto L) {
      const auto now = nf7::Env::Clock::now().time_since_epoch();
      const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now);
      lua_pushnumber(L, static_cast<double>(ms.count())/1000.);
      return 1;
    });
    lua_setfield(L, -2, "now");


    // ---- value lib ----

    // value(entity) -> value
    lua_pushcfunction(L, [](auto L) {
      if (lua_isstring(L, 2)) {
        const auto type = std::string_view {lua_tostring(L, 2)};
        if (type == "integer" || type == "int") {
          PushValue(L, static_cast<nf7::Value::Integer>(luaL_checkinteger(L, 1)));
        } else {
          return luaL_error(L, "unknown type specifier: %s", type);
        }
      } else {
        PushValue(L, CheckValue(L, 1));
      }
      return 1;
    });
    lua_setfield(L, -2, "value");

    // mvector(vector or mutable vector) -> mutable vector
    lua_pushcfunction(L, [](auto L) {
      if (auto imm = ToVector(L, 1)) {
        if (imm->use_count() == 1) {
          PushMutableVector(L, std::move(const_cast<std::vector<uint8_t>&>(**imm)));
        } else {
          PushMutableVector(L, std::vector<uint8_t> {**imm});
        }
        return 1;
      } else if (auto mut = ToMutableVector(L, 1)) {
        PushMutableVector(L, std::vector<uint8_t> {*mut});
        return 1;
      } else {
        PushMutableVector(L, {});
        return 1;
      }
    });
    lua_setfield(L, -2, "mvector");


    // ---- bit manip ----
    luaL_openlibs(L);
    luaL_loadstring(L, "return require(\"bit\")");
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "bit");
  }
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
}

}  // namespace nf7::luajit
