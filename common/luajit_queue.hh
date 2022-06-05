#pragma once

#include <memory>

#include <lua.hpp>

#include "nf7.hh"


namespace nf7::luajit {

class Queue : public File::Interface {
 public:
  using Task = std::function<void(lua_State*)>;

  static constexpr auto kPath = "$/_luajit";

  Queue() = default;
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  // thread-safe
  virtual void Push(const std::shared_ptr<nf7::Context>&, Task&&) noexcept = 0;

  virtual std::shared_ptr<Queue> self() noexcept = 0;
};

}  // namespace nf7::luajit
