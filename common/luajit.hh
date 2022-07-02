#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <lua.hpp>

#include "common/value.hh"


namespace nf7::luajit {

void PushGlobalTable(lua_State*) noexcept;
void PushImmEnv(lua_State*) noexcept;
void PushValue(lua_State*, const nf7::Value&) noexcept;
void PushVector(lua_State*, const nf7::Value::Vector&) noexcept;
void PushMutableVector(lua_State*, std::vector<uint8_t>&&) noexcept;

std::optional<nf7::Value> ToValue(lua_State*, int) noexcept;
std::optional<nf7::Value::Vector> ToVector(lua_State*, int) noexcept;
std::optional<std::vector<uint8_t>> ToMutableVector(lua_State*, int) noexcept;


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
  lua_getmetatable(L, idx);
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
