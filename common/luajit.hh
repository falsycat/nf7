#pragma once

#include <memory>

#include <lua.hpp>


namespace nf7::luajit {

void PushGlobalTable(lua_State*) noexcept;
void PushImmEnv(lua_State*) noexcept;

template <typename T>
inline void PushWeakPtr(lua_State* L, const std::weak_ptr<T>& wptr) noexcept {
  new (lua_newuserdata(L, sizeof(wptr))) std::weak_ptr<T>(wptr);
}
template <typename T>
inline std::weak_ptr<T>& ToWeakPtr(lua_State* L, int idx) noexcept {
  std::weak_ptr<T>* wptr = reinterpret_cast<decltype(wptr)>(lua_touserdata(L, idx));
  return *wptr;
}
template <typename T>
inline std::shared_ptr<T> ToSharedPtr(lua_State* L, int idx) {
  if (auto ret = ToWeakPtr<T>(L, idx).lock()) {
    return ret;
  }
  luaL_error(L, "object expired: %s", typeid(T).name());
  return nullptr;
}
template <typename T>
inline void PushWeakPtrDeleter(lua_State* L, const std::weak_ptr<T>& = {}) noexcept {
  lua_pushcfunction(L, [](auto L) {
    ToWeakPtr<T>(L, 1).~weak_ptr();
    return 0;
  });
}

}  // namespace nf7
