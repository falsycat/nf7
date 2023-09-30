// No copyright
#pragma once

#include "core/gl3/context.hh"


namespace nf7::core::imgui {

class Driver {
 public:
  Driver() = default;
  virtual ~Driver() = default;

  Driver(const Driver&) = delete;
  Driver(Driver&&) = delete;
  Driver& operator=(const Driver&) = delete;
  Driver& operator=(Driver&&) = delete;

 public:
  virtual void PreUpdate(gl3::TaskContext&) noexcept { }
  virtual void Update(gl3::TaskContext&) noexcept { }
  virtual void PostUpdate(gl3::TaskContext&) noexcept { }
};

}  // namespace nf7::core::imgui
