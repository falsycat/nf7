#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <lua.hpp>

#include "common/node.hh"
#include "common/value.hh"


namespace nf7::luajit {

void PushGlobalTable(lua_State*) noexcept;
void PushImmEnv(lua_State*) noexcept;
void PushValue(lua_State*, const nf7::Value&) noexcept;
void PushVector(lua_State*, const nf7::Value::ConstVector&) noexcept;
void PushMutableVector(lua_State*, std::vector<uint8_t>&&) noexcept;
void PushNodeLambda(lua_State*,
                    const std::shared_ptr<nf7::Node::Lambda>& callee,
                    const std::weak_ptr<nf7::Node::Lambda>& caller) noexcept;

std::optional<nf7::Value> ToValue(lua_State*, int) noexcept;
std::optional<nf7::Value::ConstVector> ToVector(lua_State*, int) noexcept;
std::optional<std::vector<uint8_t>> ToMutableVector(lua_State*, int) noexcept;


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


template <typename T>
inline void PushWeakPtr(lua_State* L, const std::weak_ptr<T>& wptr) noexcept {
  new (lua_newuserdata(L, sizeof(wptr))) std::weak_ptr<T>(wptr);
}
template <typename T>
inline void PushWeakPtrDeleter(lua_State* L, const std::weak_ptr<T>& = {}) noexcept {
  lua_pushcfunction(L, [](auto L) {
    reinterpret_cast<std::weak_ptr<T>*>(lua_touserdata(L, 1))->~weak_ptr();
    return 0;
  });
}

inline bool MatchMetaName(lua_State* L, int idx, const char* type) noexcept {
  if (0 == lua_getmetatable(L, idx)) {
    return false;
  }
  luaL_getmetatable(L, type);
  const bool ret = lua_rawequal(L, -1, -2);
  lua_pop(L, 2);
  return ret;
}
template <typename T>
inline T* ToRef(lua_State* L, int idx, const char* type) noexcept {
  return MatchMetaName(L, idx, type)? reinterpret_cast<T*>(lua_touserdata(L, idx)): nullptr;
}

template <typename T>
inline std::shared_ptr<T> CheckWeakPtr(lua_State* L, int idx, const char* type) {
  auto ptr = reinterpret_cast<std::weak_ptr<T>*>(luaL_checkudata(L, idx, type));
  if (auto ret = ptr->lock()) {
    return ret;
  } else {
    luaL_error(L, "object expired: %s", typeid(T).name());
    return nullptr;
  }
}
template <typename T>
inline T& CheckRef(lua_State* L, int idx, const char* type) {
  return *reinterpret_cast<T*>(luaL_checkudata(L, idx, type));
}
inline nf7::Value CheckValue(lua_State* L, int idx) {
  auto v = ToValue(L, idx);
  if (!v) luaL_error(L, "expected nf7::Value");
  return std::move(*v);
}

}  // namespace nf7
