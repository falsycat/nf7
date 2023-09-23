// No copyright
#pragma once

#include <imgui.h>

#include <memory>

#include "iface/env.hh"

#include "core/imgui/driver.hh"


namespace nf7::core::imgui {

class Context : public subsys::Interface {
 private:
  class Impl;

 public:
  explicit Context(Env&);
  ~Context() noexcept override;

 public:
  const std::shared_ptr<Driver>& Register(
      const std::shared_ptr<Driver>& driver);

 private:
  const std::shared_ptr<Impl> impl_;
};

}  // namespace nf7::core::imgui
