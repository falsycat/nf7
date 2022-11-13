#include <lua.hpp>

#include "common/luajit.hh"


namespace nf7::luajit {

inline void PushStdTable(lua_State* L) noexcept {
  luaL_openlibs(L);

  lua_newuserdata(L, 0);
  lua_createtable(L, 0, 0);
  lua_createtable(L, 0, 0);
  {
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


    // ---- lua std libs ----
    const auto Copy =
        [L](const char* name, const char* expr, bool imm) {
          luaL_loadstring(L, expr);
          lua_call(L, 0, 1);
          if (imm) {
            PushImmTable(L);
            lua_setmetatable(L, -2);
          }
          lua_setfield(L, -2, name);
        };
    Copy("assert",       "return assert",       false);
    Copy("error",        "return error",        false);
    Copy("ipairs",       "return ipairs",       false);
    Copy("loadstring",   "return loadstring",   false);
    Copy("next",         "return next",         false);
    Copy("pairs",        "return pairs",        false);
    Copy("pcall",        "return pcall",        false);
    Copy("rawequal",     "return rawequal",     false);
    Copy("rawget",       "return rawget",       false);
    Copy("select",       "return select",       false);
    Copy("setfenv",      "return setfenv",      false);
    Copy("setmetatable", "return setmetatable", false);
    Copy("tonumber",     "return tonumber",     false);
    Copy("tostring",     "return tostring",     false);
    Copy("type",         "return type",         false);
    Copy("unpack",       "return unpack",       false);
    Copy("_VERSION",     "return _VERSION",     false);
    Copy("xpcall",       "return xpcall",       false);

    Copy("bit",       "return require(\"bit\")", true);
    Copy("coroutine", "return coroutine",        true);
    Copy("math",      "return math",             true);
    Copy("string",    "return string",           true);
    Copy("table",     "return table",            true);
  }
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
}

}  // namespace nf7::luajit
