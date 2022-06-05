#pragma once

#include <future>
#include <memory>

#include "nf7.hh"

#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"


namespace nf7::luajit {

class Obj : public nf7::File::Interface {
 public:
  Obj() = default;
  Obj(const Obj&) = delete;
  Obj(Obj&&) = delete;
  Obj& operator=(const Obj&) = delete;
  Obj& operator=(Obj&&) = delete;

  // result is registered to LUA_REGISTRY
  virtual std::shared_future<std::shared_ptr<Ref>> Build() noexcept = 0;
};

}  // namespace nf7::luajit
