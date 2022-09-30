#pragma once

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

template <typename T, typename... Args>
inline T& NewUserData(lua_State* L, Args&&... args) noexcept {
  return *(new (lua_newuserdata(L, sizeof(T))) T(std::forward<Args>(args)...));
}


// ---- reference conversion
template <typename T>
inline T* ToRef(lua_State* L, int idx, const char* type) noexcept {
  return MatchMetaName(L, idx, type)? reinterpret_cast<T*>(lua_touserdata(L, idx)): nullptr;
}
template <typename T>
inline T& CheckRef(lua_State* L, int idx, const char* type) {
  return *reinterpret_cast<T*>(luaL_checkudata(L, idx, type));
}


// ---- Value conversion
void PushValue(lua_State*, const nf7::Value&) noexcept;
std::optional<nf7::Value> ToValue(lua_State*, int) noexcept;
inline nf7::Value CheckValue(lua_State* L, int idx) {
  auto v = ToValue(L, idx);
  if (!v) luaL_error(L, "expected nf7::Value");
  return std::move(*v);
}

void PushVector(lua_State*, const nf7::Value::ConstVector&) noexcept;
inline std::optional<nf7::Value::ConstVector> ToVector(lua_State* L, int idx) noexcept {
  auto ptr = ToRef<nf7::Value::ConstVector>(L, idx, "nf7::Value::ConstVector");
  if (!ptr) return std::nullopt;
  return *ptr;
}

void PushMutableVector(lua_State*, std::vector<uint8_t>&&) noexcept;
inline std::optional<std::vector<uint8_t>> ToMutableVector(lua_State* L, int idx) noexcept {
  auto ptr = ToRef<std::vector<uint8_t>>(L, idx, "nf7::Value::MutableVector");
  if (!ptr) return std::nullopt;
  return std::move(*ptr);
}

void PushNodeRootLambda(
    lua_State*, const std::shared_ptr<nf7::NodeRootLambda>&) noexcept;
inline const std::shared_ptr<nf7::NodeRootLambda>& CheckNodeRootLambda(lua_State* L, int idx) {
  return CheckRef<std::shared_ptr<nf7::NodeRootLambda>>(L, idx, "nf7::NodeRootLambda");
}

inline void ToStringList(lua_State* L, int idx, std::vector<std::string>& v) noexcept {
  const size_t n = lua_objlen(L, idx);
  v.clear();
  v.reserve(n);
  for (int i = 1; i <= static_cast<int>(n); ++i) {
    lua_rawgeti(L, idx, i);
    if (auto str = lua_tostring(L, -1)) {
      v.push_back(str);
    }
    lua_pop(L, 1);
  }
}


// ---- overloaded Push function for template
template <typename T>
void Push(lua_State* L, T v) noexcept {
  if constexpr (std::is_integral<T>::value) {
    lua_pushinteger(L, static_cast<lua_Integer>(v));
  } else if constexpr (std::is_floating_point<T>::value) {
    lua_pushnumber(L, static_cast<lua_Number>(v));
  } else if constexpr (std::is_null_pointer<T>::value) {
    lua_pushnil(L);
  } else {
    [] <bool F = false>() { static_assert(F, "T is invalid"); }();
  }
}
inline void Push(lua_State* L, const std::string& v) noexcept {
  lua_pushstring(L, v.c_str());
}
inline void Push(lua_State* L, const Value& v) noexcept {
  luajit::PushValue(L, v);
}
inline void Push(lua_State* L, const nf7::Value::Vector& v) noexcept {
  luajit::PushVector(L, v);
}
inline void Push(lua_State* L, const std::vector<uint8_t>& v) noexcept {
  luajit::PushMutableVector(L, std::vector<uint8_t> {v});
}
inline void Push(lua_State* L, std::vector<uint8_t>&& v) noexcept {
  luajit::PushMutableVector(L, std::move(v));
}
inline void Push(lua_State* L, const std::shared_ptr<nf7::NodeRootLambda>& la) noexcept {
  luajit::PushNodeRootLambda(L, la);
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


// ---- global table
void PushGlobalTable(lua_State*) noexcept;
void PushImmEnv(lua_State*) noexcept;

}  // namespace nf7
