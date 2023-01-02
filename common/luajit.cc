#include "common/luajit.hh"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cctype>
#include <string>
#include <string_view>
#include <variant>

#include <lua.hpp>

#include "common/logger.hh"
#include "common/logger_ref.hh"
#include "common/luajit_thread.hh"


using namespace std::literals;

namespace nf7::luajit {

template <> void PushMeta<StdTable>(lua_State* L) noexcept {
  if (!luaL_newmetatable(L, MetaName<StdTable>::kValue)) {
    return;
  }
  luaL_openlibs(L);
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

    lua_newuserdata(L, 0);
    lua_createtable(L, 0, 0);
      lua_createtable(L, 0, 0);
        lua_pushcfunction(L, [](auto L) {
          Push(L, nf7::Value {nf7::Value::Pulse {}});
          return 1;
        });
        lua_setfield(L, -2, "pulse");

        lua_pushcfunction(L, [](auto L) {
          Push(L, nf7::Value {lua_toboolean(L, 1)});
          return 1;
        });
        lua_setfield(L, -2, "boolean");

        lua_pushcfunction(L, [](auto L) {
          Push(L, nf7::Value {static_cast<nf7::Value::Integer>(lua_tointeger(L, 1))});
          return 1;
        });
        lua_setfield(L, -2, "integer");

        lua_pushcfunction(L, [](auto L) {
          Push(L, nf7::Value {static_cast<nf7::Value::Scalar>(lua_tonumber(L, 1))});
          return 1;
        });
        lua_setfield(L, -2, "scalar");

        lua_pushcfunction(L, [](auto L) {
          const char* str = lua_tostring(L, 1);
          Push(L, nf7::Value {std::string_view {str? str: ""}});
          return 1;
        });
        lua_setfield(L, -2, "string");

        lua_pushcfunction(L, [](auto L) {
          Push(L, nf7::Value::Buffer {});
          return 1;
        });
        lua_setfield(L, -2, "buffer");

        lua_pushcfunction(L, ([](auto L) {
          luaL_checktype(L, 1, LUA_TTABLE);
          if (auto n = lua_objlen(L, 1)) {
            nf7::Value::Tuple::Factory tup {n};
            for (int i = 1; i <= static_cast<int>(n); ++i) {
              lua_rawgeti(L, 1, i);
              tup.Append() = Check<nf7::Value>(L, -1);
              lua_pop(L, 1);
            }
            Push(L, tup.Create());
          } else {
            lua_pushnil(L);
            while (lua_next(L, 1)) lua_pop(L, 1), ++n;

            nf7::Value::Tuple::Factory tup {n};
            lua_pushnil(L);
            while (lua_next(L, 1)) {
              const char* name = "";
              if (lua_isstring(L, -2)) {
                name = lua_tostring(L, -2);
              }
              tup[name] = Check<nf7::Value>(L, -1);
              lua_pop(L, 1);
            }
            Push(L, tup.Create());
          }
          return 1;
        }));
        lua_setfield(L, -2, "tuple");
      lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "value");


    // ---- lua std libs ----
    const auto Copy =
        [L](const char* name, const char* expr, bool imm) {
          luaL_loadstring(L, expr);
          lua_call(L, 0, 1);
          if (imm) {
            Push(L, ImmTable {});
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
}

template <> void PushMeta<std::shared_ptr<nf7::NodeRootLambda>>(lua_State* L) noexcept {
  using T = std::shared_ptr<nf7::NodeRootLambda>;
  if (!luaL_newmetatable(L, MetaName<T>::kValue)) {
    return;
  }
  lua_createtable(L, 0, 0);
  {
    // la:send(nf7, key, value)
    lua_pushcfunction(L, [](auto L) {
      auto la = Check<T>(L, 1);
      la->ExecSend(luaL_checkstring(L, 2), Check<nf7::Value>(L, 3));
      return 0;
    });
    lua_setfield(L, -2, "send");

    // la:recv(nf7, {name1, name2, ...})
    lua_pushcfunction(L, [](auto L) {
      auto la = Check<T>(L, 1);
      auto th = luajit::Thread::GetPtr(L, 2);

      const auto names = Check<std::vector<std::string>>(L, 3);
      if (names.size() == 0) {
        return 0;
      }

      auto fu = la->Select(
          std::unordered_set<std::string>(names.begin(), names.end()));
      if (fu.done()) {
        try {
          const auto& p = fu.value();
          lua_pushstring(L, p.first.c_str());
          luajit::Push(L, p.second);
          return 2;
        } catch (nf7::Exception&) {
          return 0;
        }
      } else {
        fu.ThenIf([L, th](auto& p) {
          th->ExecResume(L, p.first, p.second);
        }).template Catch<nf7::Exception>(nullptr, [L, th](nf7::Exception&) {
          th->ExecResume(L);
        });
        return th->Yield(L, la);
      }
    });
    lua_setfield(L, -2, "recv");
  }
  lua_setfield(L, -2, "__index");

  lua_pushcfunction(L, [](auto L) {
    Check<T>(L, 1).~shared_ptr();
    return 0;
  });
  lua_setfield(L, -2, "__gc");
}

void Push(lua_State* L, GlobalTable) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::GlobalTable")) {
    Push(L, StdTable {});
    lua_setfield(L, -2, "std");
  }
}
void Push(lua_State* L, ImmEnv) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::ImmEnv")) {
    lua_createtable(L, 0, 0);
    {
      Push(L, GlobalTable {});
      lua_setfield(L, -2, "__index");

      lua_pushcfunction(L, [](auto L) { return luaL_error(L, "global is immutable"); });
      lua_setfield(L, -2, "__newindex");
    }
    lua_setmetatable(L, -2);
  }
}
void Push(lua_State* L, ImmTable) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::ImmTable")) {
    lua_pushcfunction(L, [](auto L) { return luaL_error(L, "table is immutable"); });
    lua_setfield(L, -2, "__newindex");
  }
}

}  // namespace nf7::luajit
