#pragma once

#include <concepts>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <lua.hpp>

#include "common/node_root_lambda.hh"
#include "common/value.hh"


namespace nf7::luajit {

// special types for using with Push<T> functions
struct Nil         { };
struct GlobalTable { };
struct StdTable    { };
struct ImmEnv      { };
struct ImmTable    { };


// ---- utility
inline bool MatchMetaName(lua_State* L, int idx, const char* type) noexcept {
  if (0 == lua_getmetatable(L, idx)) {
    return false;
  }
  luaL_getmetatable(L, type);
  const bool ret = lua_rawequal(L, -1, -2);
  lua_pop(L, 2);
  return ret;
}


// ---- udata utility
template <typename T, typename... Args>
inline T& NewUData(lua_State* L, Args&&... args) noexcept {
  return *(new (lua_newuserdata(L, sizeof(T))) T(std::forward<Args>(args)...));
}
template <typename T>
inline T* PeekUData(lua_State* L, int idx, const char* type) noexcept {
  return MatchMetaName(L, idx, type)? reinterpret_cast<T*>(lua_touserdata(L, idx)): nullptr;
}
template <typename T>
inline T& CheckUData(lua_State* L, int idx, const char* type) {
  return *reinterpret_cast<T*>(luaL_checkudata(L, idx, type));
}


// ---- PushMeta functions
template <typename T> void PushMeta(lua_State*) noexcept;

template <> void PushMeta<StdTable>(lua_State*) noexcept;

template <> void PushMeta<nf7::Value>(lua_State*) noexcept;
template <> void PushMeta<nf7::Value::Buffer>(lua_State*) noexcept;
template <> void PushMeta<nf7::Value::Tuple>(lua_State*) noexcept;

template <> void PushMeta<std::shared_ptr<nf7::NodeRootLambda>>(lua_State*) noexcept;


template <typename T> struct MetaName;

#define DEF_(T) template <> struct MetaName<T> { static constexpr auto kValue = #T; };
DEF_(StdTable);
DEF_(nf7::Value);
DEF_(nf7::Value::Buffer);
DEF_(nf7::Value::Tuple);
DEF_(std::shared_ptr<nf7::NodeRootLambda>);
#undef DEF_

template <typename T> concept HasMeta = requires (lua_State* L, T& t) {
  MetaName<T>::kValue;
  PushMeta<T>(L);
};
static_assert(HasMeta<nf7::Value>);


// ---- Push functions
template <std::integral T>
void Push(lua_State* L, T v) noexcept {
  lua_pushinteger(L, static_cast<lua_Integer>(v));
}
template <std::floating_point T>
void Push(lua_State* L, T v) noexcept {
  lua_pushnumber(L, static_cast<lua_Number>(v));
}
inline void Push(lua_State* L, const std::string& v) noexcept {
  lua_pushstring(L, v.c_str());
}
inline void Push(lua_State* L, Nil) noexcept { lua_pushnil(L); }
inline void Push(lua_State* L, nf7::Value::Pulse) noexcept { lua_pushnil(L); }

void Push(lua_State*, GlobalTable) noexcept;
void Push(lua_State*, ImmEnv) noexcept;
void Push(lua_State*, ImmTable) noexcept;

template <HasMeta T>
void Push(lua_State* L, const T& v) noexcept {
  NewUData<T>(L, v);
  PushMeta<T>(L);
  lua_setmetatable(L, -2);
}

// pushes all args and returns a number of them
inline int PushAll(lua_State*) noexcept {
  return 0;
}
template <typename T, typename... Args>
int PushAll(lua_State* L, T v, Args&&... args) noexcept {
  if constexpr (std::is_reference<T>::value) {
    Push(L, std::forward<T>(v));
  } else {
    Push(L, v);
  }
  return 1+PushAll(L, std::forward<Args>(args)...);
}


// ---- Peek functions
template <std::integral T>
bool Peek(lua_State* L, int i, T& v) noexcept {
  if (!lua_isnumber(L, i)) return false;
  v = static_cast<T>(lua_tointeger(L, i));
  return true;
}
template <std::floating_point T>
bool Peek(lua_State* L, int i, T& v) noexcept {
  if (!lua_isnumber(L, i)) return false;
  v = static_cast<T>(lua_tonumber(L, i));
  return true;
}
inline bool Peek(lua_State* L, int i, std::string& v) noexcept {
  if (!lua_isstring(L, i)) return false;
  v = lua_tostring(L, i);;
  return true;
}
inline bool Peek(lua_State* L, int i, std::string_view& v) noexcept {
  if (!lua_isstring(L, i)) return false;
  v = lua_tostring(L, i);;
  return true;
}

inline bool Peek(lua_State* L, int idx, std::vector<std::string>& v) noexcept {
  v.clear();
  if (!lua_istable(L, idx)) {
    if (auto str = lua_tostring(L, idx)) {
      v.emplace_back(str);
    }
    return true;
  }
  const size_t n = lua_objlen(L, idx);
  v.reserve(n);
  for (int i = 1; i <= static_cast<int>(n); ++i) {
    lua_rawgeti(L, idx, i);
    if (auto str = lua_tostring(L, -1)) {
      v.push_back(str);
    }
    lua_pop(L, 1);
  }
  return true;
}

template <HasMeta T>
inline bool Peek(lua_State* L, int idx, T& v) noexcept {
  if (MatchMetaName(L, idx, MetaName<T>::kValue)) {
    v = *reinterpret_cast<T*>(lua_touserdata(L, idx));
    return true;
  }
  return false;
}


template <typename T>
std::optional<T> Peek(lua_State* L, int i) noexcept {
  T v;
  return Peek(L, i, v)? std::optional<T> {v}: std::nullopt;
}
inline void Check(lua_State* L, int i, auto& v) {
  if (!Peek(L, i, v)) {
    luaL_error(L, "incompatible cast");
  }
}
template <typename T>
T Check(lua_State* L, int i) {
  T v;
  Check(L, i, v);
  return v;
}

}  // namespace nf7
