#pragma once

#include <memory>

#include <lua.hpp>

#include "nf7.hh"

#include "common/luajit_queue.hh"
#include "common/value.hh"


namespace nf7::luajit {

class Ref final : public nf7::Value::Data {
 public:
  Ref() = delete;
  Ref(const std::shared_ptr<nf7::Context>& ctx,
      const std::shared_ptr<nf7::luajit::Queue>& q, int idx) noexcept :
      ctx_(ctx), q_(q), idx_(idx) {
  }
  Ref(const std::shared_ptr<nf7::Context>& ctx,
      const std::shared_ptr<nf7::luajit::Queue>& q, lua_State* L) noexcept :
      ctx_(ctx), q_(q), idx_(luaL_ref(L, LUA_REGISTRYINDEX)) {
  }
  ~Ref() noexcept {
    q_->Push(ctx_, [idx = idx_](auto L) { luaL_unref(L, LUA_REGISTRYINDEX, idx); });
  }
  Ref(const Ref&) = delete;
  Ref(Ref&&) = delete;
  Ref& operator=(const Ref&) = delete;
  Ref& operator=(Ref&&) = delete;

  void PushSelf(lua_State* L) noexcept {
    lua_rawgeti(L, LUA_REGISTRYINDEX, idx_);
  }

  int index() const noexcept { return idx_; }
  const std::shared_ptr<nf7::luajit::Queue>& ljq() const noexcept { return q_; }

 private:
  std::shared_ptr<nf7::Context> ctx_;
  std::shared_ptr<nf7::luajit::Queue> q_;
  int idx_;
};

}  // namespace nf7::luajit
