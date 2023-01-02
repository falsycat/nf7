#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include <lua.hpp>

#include "common/luajit.hh"
#include "common/value.hh"


namespace nf7::luajit {

static auto SwitchByNumericTypeName(std::string_view type, auto func) {
  if (type == "u8")  { uint8_t  t = 0; return func(t); }
  if (type == "u16") { uint16_t t = 0; return func(t); }
  if (type == "u32") { uint32_t t = 0; return func(t); }
  if (type == "u64") { uint64_t t = 0; return func(t); }
  if (type == "s8")  { int8_t   t = 0; return func(t); }
  if (type == "s16") { int16_t  t = 0; return func(t); }
  if (type == "s32") { int32_t  t = 0; return func(t); }
  if (type == "s64") { int64_t  t = 0; return func(t); }
  if (type == "f32") { float    t = 0; return func(t); }
  if (type == "f64") { double   t = 0; return func(t); }
  throw nf7::Exception {"unknown numeric type name: "+std::string {type}};
}

template <typename T>
static size_t PushArrayFromBytes(
    lua_State* L, size_t n, const uint8_t* ptr, const uint8_t* end);
template <typename T>
static size_t PushFromBytes(lua_State* L, const uint8_t* ptr, const uint8_t* end);
template <typename T>
static size_t ToBytes(lua_State* L, uint8_t* ptr, uint8_t* end);


template <> void PushMeta<nf7::Value>(lua_State* L) noexcept {
  if (luaL_newmetatable(L, MetaName<nf7::Value>::kValue)) {
    lua_createtable(L, 0, 0);
      lua_pushcfunction(L, [](auto L) {
        const auto& v = Check<nf7::Value>(L, 1);
        lua_pushstring(L, v.typeName());
        return 1;
      });
      lua_setfield(L, -2, "type");

      lua_pushcfunction(L, [](auto L) {
        const auto& v = Check<nf7::Value>(L, 1);
        std::visit([L](auto& v) { luajit::Push(L, v); }, v.value());
        return 1;
      });
      lua_setfield(L, -2, "get");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      Check<nf7::Value>(L, 1).~Value();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
}

template <> void PushMeta<nf7::Value::Buffer>(lua_State* L) noexcept {
  if (luaL_newmetatable(L, MetaName<nf7::Value::Buffer>::kValue)) {
    lua_createtable(L, 0, 0);
      lua_pushcfunction(L, [](auto L) {
        const auto& v = Check<nf7::Value::Buffer>(L, 1);

        const auto offset = luaL_checkinteger(L, 2);
        if (offset < 0) {
          return luaL_error(L, "negative offset");
        }
        if (offset > static_cast<lua_Integer>(v.size())) {
          return luaL_error(L, "offset overflow");
        }

        const uint8_t* ptr = v.ptr() + offset;
        const uint8_t* end = v.ptr() + v.size();

        const auto top = lua_gettop(L);
        for (int i = 3; i <= top; ++i) {
          const std::string_view type = luaL_checkstring(L, i);

          std::optional<size_t> n;
          if (lua_isnumber(L, i+1)) {
            const auto ni = lua_tointeger(L, i+1);
            if (ni < 0) {
              return luaL_error(L, "negative size");
            }
            ++i;
          }

          SwitchByNumericTypeName(type, [&](auto t) {
            if (n) {
              ptr += PushArrayFromBytes<decltype(t)>(L, *n, ptr, end);
            } else {
              ptr += PushFromBytes<decltype(t)>(L, ptr, end);
            }
          });
        }
        return top-2;
      });
      lua_setfield(L, -2, "get");

      lua_pushcfunction(L, [](auto L) {
        const auto& v = Check<nf7::Value::Buffer>(L, 1);
        lua_pushlstring(L, reinterpret_cast<const char*>(v.ptr()), v.size());
        return 1;
      });
      lua_setfield(L, -2, "str");

      lua_pushcfunction(L, [](auto L) {
        Push(L, Check<nf7::Value::Buffer>(L, 1).size());
        return 1;
      });
      lua_setfield(L, -2, "size");
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      Check<nf7::Value::Buffer>(L, 1).~Buffer();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
}

template <> void PushMeta<nf7::Value::Tuple>(lua_State* L) noexcept {
  if (luaL_newmetatable(L, MetaName<nf7::Value::Tuple>::kValue)) {
    lua_pushcfunction(L, [](auto L) {
      const auto& v = Check<nf7::Value::Tuple>(L, 1);
      try {
        if (lua_isnumber(L, 2)) {
          Push(L, v[*Peek<size_t>(L, 2)-1]);
        } else if (lua_isstring(L, 2)) {
          Push(L, v[*Peek<std::string_view>(L, 2)]);
        } else {
          return luaL_error(L, "expected number or string as tuple index");
        }
        return 1;
      } catch (nf7::Value::Exception& e) {
        return luaL_error(L, "%s", e.msg().c_str());
      }
    });
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      Push(L, Check<nf7::Value::Tuple>(L, 1).size());
      return 1;
    });
    lua_setfield(L, -2, "__len");

    lua_pushcfunction(L, [](auto L) {
      Check<nf7::Value::Tuple>(L, 1).~Tuple();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
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
    if constexpr (std::is_integral_v<T>) {
      lua_pushinteger(L, static_cast<lua_Integer>(*reinterpret_cast<const T*>(ptr)));
    } else if constexpr (std::is_floating_point_v<T>) {
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
  if constexpr (std::is_integral_v<T>) {
    lua_pushinteger(L, static_cast<lua_Integer>(*reinterpret_cast<const T*>(ptr)));
  } else if constexpr (std::is_floating_point_v<T>) {
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
