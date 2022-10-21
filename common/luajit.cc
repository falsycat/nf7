#include "common/luajit.hh"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cctype>
#include <string>
#include <string_view>

#include <lua.hpp>

#include "common/logger.hh"
#include "common/logger_ref.hh"
#include "common/luajit_thread.hh"
#include "common/luajit_std.hh"


namespace nf7::luajit {

// buffer <-> lua value conversion
template <typename T>
static size_t PushArrayFromBytes(
    lua_State* L, size_t n, const uint8_t* ptr, const uint8_t* end);
template <typename T>
static size_t PushFromBytes(lua_State* L, const uint8_t* ptr, const uint8_t* end);
template <typename T>
static size_t ToBytes(lua_State* L, uint8_t* ptr, uint8_t* end);


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
          auto operator()(Value::Vector)  noexcept { PushVector(L, v.vector()); }
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
std::optional<nf7::Value> ToValue(lua_State* L, int idx) noexcept {
  // get absolute position on stack because recursion call may occur
  if (idx < 0) {
    idx = lua_gettop(L)+idx+1;
  }

  if (lua_isnoneornil(L, idx)) {
    return nf7::Value {nf7::Value::Pulse {}};
  }
  if (lua_isnumber(L, idx)) {
    const double n = lua_tonumber(L, idx);
    return nf7::Value {n};
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
      std::string name;
      if (lua_type(L, -2) == LUA_TSTRING) {
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

void PushVector(lua_State* L, const nf7::Value::ConstVector& v) noexcept {
  static const char* kTypeName = "nf7::Value::ConstVector";
  using T = nf7::Value::ConstVector;

  assert(v != nullptr);
  new (lua_newuserdata(L, sizeof(v))) nf7::Value::ConstVector(v);

  if (luaL_newmetatable(L, kTypeName)) {
    lua_createtable(L, 0, 0);
      lua_pushcfunction(L, [](auto L) {
        const auto& v = CheckRef<T>(L, 1, kTypeName);
        const auto offset = luaL_checkinteger(L, 2);
        if (offset < 0) {
          return luaL_error(L, "negative offset");
        }
        if (offset > static_cast<lua_Integer>(v->size())) {
          return luaL_error(L, "offset overflow");
        }

        const uint8_t* ptr = v->data() + offset;
        const uint8_t* end = v->data() + v->size();

        luaL_checktype(L, 3, LUA_TTABLE);
        const int ecnt = static_cast<int>(lua_objlen(L, 3));
        lua_createtable(L, ecnt, 0);

        for (int i = 1; i <= ecnt; ++i) {
          lua_rawgeti(L, 3, i);
          if (lua_istable(L, -1)) {  // array
            lua_rawgeti(L, -1, 1);
            const std::string_view type = luaL_checkstring(L, -1);
            lua_rawgeti(L, -2, 2);
            const size_t n = static_cast<size_t>(luaL_checkinteger(L, -1));
            lua_pop(L, 2);

            if (type == "u8") {
              ptr += PushArrayFromBytes<uint8_t>(L, n, ptr, end);
            } else if (type == "u16") {
              ptr += PushArrayFromBytes<uint16_t>(L, n, ptr, end);
            } else if (type == "u32") {
              ptr += PushArrayFromBytes<uint32_t>(L, n, ptr, end);
            } else if (type == "u64") {
              ptr += PushArrayFromBytes<uint64_t>(L, n, ptr, end);
            } else if (type == "s8") {
              ptr += PushArrayFromBytes<int8_t>(L, n, ptr, end);
            } else if (type == "s16") {
              ptr += PushArrayFromBytes<int16_t>(L, n, ptr, end);
            } else if (type == "s32") {
              ptr += PushArrayFromBytes<int32_t>(L, n, ptr, end);
            } else if (type == "s64") {
              ptr += PushArrayFromBytes<int64_t>(L, n, ptr, end);
            } else if (type == "f32") {
              ptr += PushArrayFromBytes<float>(L, n, ptr, end);
            } else if (type == "f64") {
              ptr += PushArrayFromBytes<double>(L, n, ptr, end);
            }
          } else if (lua_isstring(L, -1)) {  // single
            const std::string_view type = lua_tostring(L, -1);
            if (type == "u8") {
              ptr += PushFromBytes<uint8_t>(L, ptr, end);
            } else if (type == "u16") {
              ptr += PushFromBytes<uint16_t>(L, ptr, end);
            } else if (type == "u32") {
              ptr += PushFromBytes<uint32_t>(L, ptr, end);
            } else if (type == "u64") {
              ptr += PushFromBytes<uint64_t>(L, ptr, end);
            } else if (type == "s8") {
              ptr += PushFromBytes<int8_t>(L, ptr, end);
            } else if (type == "s16") {
              ptr += PushFromBytes<int16_t>(L, ptr, end);
            } else if (type == "s32") {
              ptr += PushFromBytes<int32_t>(L, ptr, end);
            } else if (type == "s64") {
              ptr += PushFromBytes<int64_t>(L, ptr, end);
            } else if (type == "f32") {
              ptr += PushFromBytes<float>(L, ptr, end);
            } else if (type == "f64") {
              ptr += PushFromBytes<double>(L, ptr, end);
            }
          } else {
            return luaL_error(L, "unknown type specifier at index: %d", i);
          }
          lua_rawseti(L, -3, i);
          lua_pop(L, 1);
        }
        return 1;
      });
      lua_setfield(L, -2, "get");

      lua_pushcfunction(L, [](auto L) {
        const auto& v = CheckRef<T>(L, 1, kTypeName);
        lua_pushlstring(L, reinterpret_cast<const char*>(v->data()), v->size());
        return 1;
      });
      lua_setfield(L, -2, "str");

      lua_pushcfunction(L, [](auto L) {
        const auto& v = CheckRef<T>(L, 1, kTypeName);
        lua_pushinteger(L, static_cast<lua_Integer>(v->size()));
        return 1;
      });
      lua_setfield(L, -2, "size");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      CheckRef<T>(L, 1, kTypeName).~shared_ptr();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}

void PushMutableVector(lua_State* L, std::vector<uint8_t>&& v) noexcept {
  constexpr const char* kTypeName = "nf7::Value::MutableVector";
  using T = std::vector<uint8_t>;

  new (lua_newuserdata(L, sizeof(v))) T(std::move(v));
  if (luaL_newmetatable(L, kTypeName)) {
    lua_createtable(L, 0, 0);
      lua_pushcfunction(L, [](auto L) {
        auto& v = CheckRef<T>(L, 1, kTypeName);
        const lua_Integer offset = luaL_checkinteger(L, 2);
        if (offset < 0) return luaL_error(L, "negative offset");

        luaL_checktype(L, 3, LUA_TTABLE);
        const int len = static_cast<int>(lua_objlen(L, 3));

        uint8_t* ptr = v.data() + offset;
        uint8_t* end = v.data() + v.size();

        for (int i = 1; i <= len; ++i) {
          lua_rawgeti(L, 3, i);
          lua_rawgeti(L, -1, 1);
          lua_rawgeti(L, -2, 2);

          const std::string_view type = lua_tostring(L, -2);
          if (type == "u8") {
            ptr += ToBytes<uint8_t>(L, ptr, end);
          } else if (type == "u16") {
            ptr += ToBytes<uint16_t>(L, ptr, end);
          } else if (type == "u32") {
            ptr += ToBytes<uint32_t>(L, ptr, end);
          } else if (type == "u64") {
            ptr += ToBytes<uint64_t>(L, ptr, end);
          } else if (type == "s8") {
            ptr += ToBytes<int8_t>(L, ptr, end);
          } else if (type == "s16") {
            ptr += ToBytes<int16_t>(L, ptr, end);
          } else if (type == "s32") {
            ptr += ToBytes<int32_t>(L, ptr, end);
          } else if (type == "s64") {
            ptr += ToBytes<int64_t>(L, ptr, end);
          } else if (type == "f32") {
            ptr += ToBytes<float>(L, ptr, end);
          } else if (type == "f64") {
            ptr += ToBytes<double>(L, ptr, end);
          }
          lua_pop(L, 3);
        }
        return 0;
      });
      lua_setfield(L, -2, "set");

      lua_pushcfunction(L, [](auto L) {
        auto& v = CheckRef<T>(L, 1, kTypeName);
        const lua_Integer size = luaL_checkinteger(L, 2);
        if (size < 0) return luaL_error(L, "negative size");
        v.resize(static_cast<size_t>(size));
        return 0;
      });
      lua_setfield(L, -2, "resize");

      lua_pushcfunction(L, [](auto L) {
        auto&      dst     = CheckRef<T>(L, 1, kTypeName);
        const auto dst_off = luaL_checkinteger(L, 2);

        const T* src;
        if (const auto& v = ToVector(L, 3)) {
          src = &**v;
        } else if (const auto& mv = ToMutableVector(L, 3)) {
          src = &*mv;
        } else {
          return luaL_error(L, "#2 argument must be vector or mutable vector");
        }
        const auto src_off = luaL_checkinteger(L, 4);

        const lua_Integer size = luaL_checkinteger(L, 5);
        if (size < 0) {
          return luaL_error(L, "negative size");
        }
        if (dst_off < 0 || static_cast<size_t>(dst_off+size) > dst.size()) {
          return luaL_error(L, "dst out of bounds");
        }
        if (src_off < 0 || static_cast<size_t>(src_off+size) > src->size()) {
          return luaL_error(L, "src out of bounds");
        }
        std::memcpy(dst. data()+static_cast<size_t>(dst_off),
                    src->data()+static_cast<size_t>(src_off),
                    static_cast<size_t>(size));
        return 0;
      });
      lua_setfield(L, -2, "blit");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      CheckRef<T>(L, 1, kTypeName).~vector();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}

void PushNodeRootLambda(
    lua_State* L, const std::shared_ptr<nf7::NodeRootLambda>& la) noexcept {
  assert(la);

  using T = std::shared_ptr<nf7::NodeRootLambda>;
  new (lua_newuserdata(L, sizeof(T))) T {la};

  if (luaL_newmetatable(L, "nf7::NodeRootLambda")) {
    lua_createtable(L, 0, 0);
    {
      // la:send(nf7, key, value)
      lua_pushcfunction(L, [](auto L) {
        auto la = CheckNodeRootLambda(L, 1);
        la->ExecSend(luaL_checkstring(L, 2), luajit::CheckValue(L, 3));
        return 0;
      });
      lua_setfield(L, -2, "send");

      // la:recv(nf7, {name1, name2, ...})
      lua_pushcfunction(L, [](auto L) {
        auto la = CheckNodeRootLambda(L, 1);
        auto th = luajit::Thread::GetPtr(L, 2);

        std::vector<std::string> names;
        ToStringList(L, 3, names);
        if (names.size() == 0) {
          return 0;
        }

        auto fu = la->Select(
            std::unordered_set<std::string>(names.begin(), names.end()));
        if (fu.done()) {
          try {
            const auto& p = fu.value();
            lua_pushstring(L, p.first.c_str());
            luajit::PushValue(L, p.second);
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
          return th->Yield(L);
        }
      });
      lua_setfield(L, -2, "recv");
    }
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      CheckNodeRootLambda(L, 1).~T();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}


void PushGlobalTable(lua_State* L) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::GlobalTable")) {
    PushStdTable(L);
    lua_setfield(L, -2, "std");
  }
}
void PushImmEnv(lua_State* L) noexcept {
  if (luaL_newmetatable(L, "nf7::luajit::ImmEnv")) {
    lua_createtable(L, 0, 0);
    {
      PushGlobalTable(L);
      lua_setfield(L, -2, "__index");

      lua_pushcfunction(L, [](auto L) { return luaL_error(L, "global is immutable"); });
      lua_setfield(L, -2, "__newindex");
    }
    lua_setmetatable(L, -2);
  }
}


template <typename T>
static size_t PushArrayFromBytes(lua_State* L, size_t n, const uint8_t* ptr, const uint8_t* end) {
  const size_t size = n*sizeof(T);
  if (ptr + size > end) {
    luaL_error(L, "bytes shortage");
    return 0;
  }
  lua_createtable(L, static_cast<int>(n), 0);
  for (size_t i = 0; i < n; ++i) {
    if constexpr (std::is_integral<T>::value) {
      lua_pushinteger(L, static_cast<lua_Integer>(*reinterpret_cast<const T*>(ptr)));
    } else if constexpr (std::is_floating_point<T>::value) {
      lua_pushnumber(L, static_cast<lua_Number>(*reinterpret_cast<const T*>(ptr)));
    } else {
      [] <bool F = false>() { static_assert(F, "T is invalid"); }();
    }
    lua_rawseti(L, -2, static_cast<int>(i + 1));
    ptr += sizeof(T);
  }
  return size;
}
template <typename T>
static size_t PushFromBytes(lua_State* L, const uint8_t* ptr, const uint8_t* end) {
  const size_t size = sizeof(T);
  if (ptr + size > end) {
    luaL_error(L, "bytes shortage");
    return 0;
  }
  if constexpr (std::is_integral<T>::value) {
    lua_pushinteger(L, static_cast<lua_Integer>(*reinterpret_cast<const T*>(ptr)));
  } else if constexpr (std::is_floating_point<T>::value) {
    lua_pushnumber(L, static_cast<lua_Number>(*reinterpret_cast<const T*>(ptr)));
  } else {
    [] <bool F = false>() { static_assert(F, "T is invalid"); }();
  }
  return size;
}
template <typename T>
static size_t ToBytes(lua_State* L, uint8_t* ptr, uint8_t* end) {
  if (lua_istable(L, -1)) {
    const size_t len  = lua_objlen(L, -1);
    const size_t size = sizeof(T)*len;
    if (ptr + size > end) {
      luaL_error(L, "buffer size overflow");
      return 0;
    }
    for (size_t i = 0; i < len; ++i) {
      lua_rawgeti(L, -1, static_cast<int>(i+1));
      if constexpr (std::is_integral<T>::value) {
        *reinterpret_cast<T*>(ptr) = static_cast<T>(lua_tointeger(L, -1));
      } else if constexpr (std::is_floating_point<T>::value) {
        *reinterpret_cast<T*>(ptr) = static_cast<T>(lua_tonumber(L, -1));
      } else {
        [] <bool F = false>() { static_assert(F, "T is invalid"); }();
      }
      lua_pop(L, 1);
      ptr += sizeof(T);
    }
    return size;
  } else if (lua_isnumber(L, -1)) {
    if (ptr + sizeof(T) > end) {
      luaL_error(L, "buffer size overflow");
      return 0;
    }
    if constexpr (std::is_integral<T>::value) {
      *reinterpret_cast<T*>(ptr) = static_cast<T>(lua_tointeger(L, -1));
    } else if constexpr (std::is_floating_point<T>::value) {
      *reinterpret_cast<T*>(ptr) = static_cast<T>(lua_tonumber(L, -1));
    } else {
      [] <bool F = false>() { static_assert(F, "T is invalid"); }();
    }
    return sizeof(T);
  } else if (lua_isstring(L, -1)) {
    if constexpr (std::is_same<T,  uint8_t>::value) {
      size_t sz;
      const char* str = lua_tolstring(L, -1, &sz);
      std::memcpy(ptr, str, std::min(static_cast<size_t>(end-ptr), sz));
      return sz;
    } else {
      luaL_error(L, "string can be specified for only u8 type");
      return 0;
    }
  } else {
    luaL_error(L, "number or array expected");
    return 0;
  }
}

}  // namespace nf7::luajit
