#pragma once

#include <memory>

#include <lua.hpp>

#include "nf7.hh"

#include "common/luajit_queue.hh"


namespace nf7::luajit {

class Ref final {
 public:
  Ref() = delete;
  Ref(const std::shared_ptr<nf7::Context>& ctx,
      const std::shared_ptr<nf7::luajit::Queue>& q, int idx) noexcept :
      ctx_(ctx), q_(q), idx_(idx) {
  }
  ~Ref() noexcept {
    q_->Push(ctx_, [idx = idx_](auto L) { luaL_unref(L, LUA_REGISTRYINDEX, idx); });
  }
  Ref(const Ref&) = delete;
  Ref(Ref&&) = delete;
  Ref& operator=(const Ref&) = delete;
  Ref& operator=(Ref&&) = delete;

  int index() const noexcept { return idx_; }
  const std::shared_ptr<nf7::luajit::Queue>& ljq() const noexcept { return q_; }

 private:
  std::shared_ptr<nf7::Context> ctx_;
  std::shared_ptr<nf7::luajit::Queue> q_;
  int idx_;
};

}  // namespace nf7::luajit
