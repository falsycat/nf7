// No copyright
#include "core/luajit/thread.hh"

#include <string>

#include "core/luajit/context.hh"


namespace nf7::core::luajit {

void Thread::SetUpThread() noexcept {
  luaL_newmetatable(th_, Context::kGlobalTableName);
  {
    static const char* kName = "nf7::core::luajit::Thread";

    new (lua_newuserdata(th_, sizeof(this))) Thread* {this};
    if (luaL_newmetatable(th_, kName)) {
      lua_createtable(th_, 0, 0);
      {
        lua_pushcfunction(th_, [](auto L) {
          luaL_checkudata(L, 1, kName);
          return luaL_error(L, lua_tostring(L, 2));
        });
        lua_setfield(th_, -2, "throw");

        lua_pushcfunction(th_, [](auto L) {
          luaL_checkudata(L, 1, kName);
          if (lua_toboolean(L, 2)) {
            return 0;
          } else {
            return luaL_error(L, "assertion failure");
          }
        });
        lua_setfield(th_, -2, "assert");

        lua_pushcfunction(th_, ([](auto L) {
          auto th = TaskContext::CheckUserData<Thread*>(L, 1, kName);

          TaskContext lua {th->context_, L};

          const auto type = std::string {luaL_optstring(L, 3, "")};
          if (type.empty()) {
            switch (lua_type(L, 2)) {
            case LUA_TNONE:
            case LUA_TNIL:
              lua.Push(nf7::Value {});
              break;
            case LUA_TNUMBER:
              lua.Push(nf7::Value {
                       static_cast<nf7::Value::Real>(lua_tonumber(L, 2))});
              break;
            case LUA_TSTRING: {
              size_t len;
              const auto ptr = lua_tolstring(L, 2, &len);
              lua.Push(nf7::Value::MakeBuffer(ptr, ptr+len));
            } break;
            case LUA_TUSERDATA:
              lua.Push(lua.CheckValue(2));
              break;
            default:
              return luaL_error(L, "invalid type to make a value");
            }
          } else {
            if ("null" == type) {
              lua.Push(nf7::Value {});
            } else if ("integer" == type) {
              lua.Push(nf7::Value {
                       static_cast<nf7::Value::Integer>(lua_tointeger(L, 2))});
            } else if ("real" == type) {
              lua.Push(nf7::Value {
                       static_cast<nf7::Value::Real>(lua_tonumber(L, 2))});
            } else if ("buffer" == type) {
              size_t len;
              const auto ptr = lua_tolstring(L, 2, &len);
              lua.Push(nf7::Value::MakeBuffer(ptr, ptr+len));
            } else {
              return luaL_error(L, "unknown type specifier: %s", type.c_str());
            }
          }
          return 1;
        }));
        lua_setfield(th_, -2, "value");
      }
      lua_setfield(th_, -2, "__index");
    }
    lua_setmetatable(th_, -2);
    lua_setfield(th_, -2, "nf7");
  }
  lua_pop(th_, 1);
}

}  // namespace nf7::core::luajit
