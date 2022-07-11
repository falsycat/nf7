#include "common/luajit.hh"

#include <cassert>

#include <lua.hpp>

#include "common/logger.hh"
#include "common/logger_ref.hh"
#include "common/luajit_thread.hh"


namespace nf7::luajit {

void PushGlobalTable(lua_State* L) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::PushGlobalTable")) {
    lua_pushcfunction(L, [](auto L) {
      PushValue(L, CheckValue(L, 1));
      return 1;
    });
    lua_setfield(L, -2, "nf7_Value");

    lua_pushcfunction(L, [](auto L) {
      if (auto imm = ToVector(L, 1)) {
        return 1;
      }
      if (auto mut = ToMutableVector(L, 1)) {
        PushVector(L, std::make_shared<std::vector<uint8_t>>(std::move(*mut)));
        return 1;
      }
      return luaL_error(L, "expected nf7::Value::MutableVector or nf7::Value::Vector");
    });
    lua_setfield(L, -2, "nf7_Vector");

    lua_pushcfunction(L, [](auto L) {
      if (auto imm = ToVector(L, 1)) {
        if (imm->use_count() == 1) {
          PushMutableVector(L, std::move(**imm));
        } else {
          PushMutableVector(L, std::vector<uint8_t> {**imm});
        }
        return 1;
      }
      if (auto mut = ToMutableVector(L, 1)) {
        PushMutableVector(L, std::vector<uint8_t> {*mut});
        return 1;
      }
      PushMutableVector(L, {});
      return 1;
    });
    lua_setfield(L, -2, "nf7_MutableVector");
  }
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
    lua_createtable(L, 0, 0);
      lua_pushcfunction(L, [](auto L) {
        const auto& v = CheckRef<nf7::Value>(L, 1, "nf7::Value");
        lua_pushstring(L, v.typeName());
        return 1;
      });
      lua_setfield(L, -2, "type");

      lua_pushcfunction(L, [](auto L) {
        const auto& v = CheckRef<nf7::Value>(L, 1, "nf7::Value");

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

          auto operator()(Value::Tuple) noexcept {
            const auto& tup = *v.tuple();
            lua_createtable(L, 0, 0);
            size_t arridx = 0;
            for (auto& p : tup) {
              PushValue(L, p.second);
              if (p.first.empty()) {
                lua_rawseti(L, -2, static_cast<int>(arridx++));
              } else {
                lua_setfield(L, -2, p.first.c_str());
              }
            }
          }
        };
        v.Visit(Visitor{.L = L, .v = v});
        return 1;
      });
      lua_setfield(L, -2, "value");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      CheckRef<nf7::Value>(L, 1, "nf7::Value").~Value();
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
        const auto& v = CheckRef<nf7::Value::Vector>(L, 1, "nf7::Value::Vector");
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
      CheckRef<nf7::Value::Vector>(L, 1, "nf7::Value::Vector").~shared_ptr();
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
        auto& v = CheckRef<std::vector<uint8_t>>(L, 1, "nf7::Value::MutableVector");
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
        auto& v = CheckRef<std::vector<uint8_t>>(L, 1, "nf7::Value::MutableVector");
        const lua_Integer size = luaL_checkinteger(L, 2);
        if (size < 0) return luaL_error(L, "negative size");
        v.resize(static_cast<size_t>(size));
        return 0;
      });
      lua_setfield(L, -2, "truncate");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      CheckRef<std::vector<uint8_t>>(L, 1, "nf7::Value::MutableVector").~vector();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}

std::optional<nf7::Value> ToValue(lua_State* L, int idx) noexcept {
  if (lua_isnil(L, idx)) {
    return nf7::Value {nf7::Value::Pulse {}};
  }
  if (lua_isnumber(L, idx)) {
    const double n = lua_tonumber(L, idx);
    const auto   i = static_cast<nf7::Value::Integer>(n);
    return n == static_cast<double>(i)? nf7::Value {i}: nf7::Value{n};
  }
  if (lua_isboolean(L, idx)) {
    return nf7::Value {bool {!!lua_toboolean(L, idx)}};
  }
  if (lua_isstring(L, idx)) {
    size_t len;
    const char* str = lua_tolstring(L, idx, &len);
    return nf7::Value {std::string {str, len}};
  }
  if (auto vec = ToVector(L, idx)) {
    return nf7::Value {std::move(*vec)};
  }
  if (auto vec = ToMutableVector(L, idx)) {
    return nf7::Value {std::move(*vec)};
  }
  if (lua_istable(L, idx)) {
    std::vector<nf7::Value::TuplePair> tup;
    lua_pushnil(L);
    while (lua_next(L, idx)) {
      std::string name = "";
      if (lua_isstring(L, -2)) {
        name = lua_tostring(L, -2);
      }
      auto val = ToValue(L, -1);
      if (!val) return std::nullopt;
      tup.push_back({std::move(name), std::move(*val)});
      lua_pop(L, 1);
    }
    return nf7::Value {std::move(tup)};
  }
  if (auto val = ToRef<nf7::Value>(L, idx, "nf7::Value")) {
    return *val;
  }
  return std::nullopt;
}
std::optional<nf7::Value::Vector> ToVector(lua_State* L, int idx) noexcept {
  auto ptr = ToRef<nf7::Value::Vector>(L, idx, "nf7::Value::Vector");
  if (!ptr) return std::nullopt;
  return *ptr;
}
std::optional<std::vector<uint8_t>> ToMutableVector(lua_State* L, int idx) noexcept {
  auto ptr = ToRef<std::vector<uint8_t>>(L, idx, "nf7::Value::MutableVector");
  if (!ptr) return std::nullopt;
  return std::move(*ptr);
}

}  // namespace nf7::luajit
