#pragma once

#include <memory>

#include <lua.hpp>

#include "nf7.hh"


namespace nf7::lj {

class Queue : public File::Interface {
 public:
  using Task = std::function<void(lua_State*)>;

  Queue() = default;
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  virtual void Push(const std::shared_ptr<nf7::Context>&, Task&&) noexcept = 0;
};

}  // namespace nf7::lj
