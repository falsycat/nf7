#include "common/luajit.hh"

#include <cassert>

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

void PushValue(lua_State* L, const nf7::Value& v) noexcept {
  new (lua_newuserdata(L, sizeof(v))) nf7::Value(v);

  if (luaL_newmetatable(L, "nf7::Value")) {
    lua_pushcfunction(L, [](auto L) {
      const auto& v = ToRef<nf7::Value>(L, 1, "nf7::Value");
      lua_pushstring(L, v.typeName());

      struct Visitor final {
        lua_State*        L;
        const nf7::Value& v;
        auto operator()(Value::Pulse)   noexcept { lua_pushnil(L); }
        auto operator()(Value::Boolean) noexcept { lua_pushboolean(L, v.boolean()); }
        auto operator()(Value::Integer) noexcept { lua_pushinteger(L, v.integer()); }
        auto operator()(Value::Scalar)  noexcept { lua_pushnumber(L, v.scalar()); }
        auto operator()(Value::String)  noexcept { lua_pushstring(L, v.string().c_str()); }
        auto operator()(Value::Vector)  noexcept { lua_pushnil(L); }
        auto operator()(Value::DataPtr) noexcept { lua_pushnil(L); }
      };
      v.Visit(Visitor{.L = L, .v = v});
      return 2;
    });
    lua_setfield(L, -2, "__len");

    lua_pushcfunction(L, [](auto L) {
      ToRef<nf7::Value>(L, 1, "nf7::Value").~Value();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}

void PushVector(lua_State* L, const nf7::Value::Vector& v) noexcept {
  assert(v);
  new (lua_newuserdata(L, sizeof(v))) nf7::Value::Vector(v);

  // TODO: separate const vector and mutable vector

  if (luaL_newmetatable(L, "nf7::Value::Vector")) {
    lua_createtable(L, 0, 0);
      lua_pushcfunction(L, [](auto L) {
        const auto& v = ToRef<nf7::Value::Vector>(L, 1, "nf7::Value::Vector");
        const lua_Integer offset = luaL_checkinteger(L, 2);
        const lua_Integer size   = luaL_checkinteger(L, 3);
        if (offset < 0) return luaL_error(L, "negative offset");
        if (size   < 0) return luaL_error(L, "negative size");

        if (static_cast<size_t>(offset + size) > v->size()) {
          return luaL_error(L, "size overflow");
        }

        lua_pushlstring(L, reinterpret_cast<const char*>(v->data()+offset), static_cast<size_t>(size));
        return 1;
      });
      lua_setfield(L, -2, "fetch");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      ToRef<nf7::Value::Vector>(L, 1, "nf7::Value::Vector").~shared_ptr();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
}

void PushMutableVector(lua_State* L, std::vector<uint8_t>&& v) noexcept {
  new (lua_newuserdata(L, sizeof(v))) std::vector<uint8_t>(std::move(v));

  if (luaL_newmetatable(L, "nf7::Value::MutableVector")) {
    lua_createtable(L, 0, 0);
      lua_pushcfunction(L, [](auto L) {
        auto& v = ToRef<std::vector<uint8_t>>(L, 1, "nf7::Value::MutableVector");
        const lua_Integer offset = luaL_checkinteger(L, 2);
        if (offset < 0) return luaL_error(L, "negative offset");

        size_t size;
        const char* buf = luaL_checklstring(L, 3, &size);
        if (static_cast<size_t>(offset) + size > v.size()) {
          return luaL_error(L, "size overflow");
        }

        std::memcpy(v.data()+offset, buf, size);
        return 0;
      });
      lua_setfield(L, -2, "blit");

      lua_pushcfunction(L, [](auto L) {
        auto& v = ToRef<std::vector<uint8_t>>(L, 1, "nf7::Value::MutableVector");
        const lua_Integer size = luaL_checkinteger(L, 2);
        if (size < 0) return luaL_error(L, "negative size");
        v.resize(static_cast<size_t>(size));
        return 0;
      });
      lua_setfield(L, -2, "truncate");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      ToRef<std::vector<uint8_t>>(L, 1, "nf7::Value::MutableVector").~vector();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
}

}  // namespace nf7::luajit
