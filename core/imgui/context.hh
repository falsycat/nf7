// No copyright
#pragma once

#include <imgui.h>

#include <memory>

#include "iface/common/future.hh"
#include "iface/env.hh"

#include "core/imgui/driver.hh"
#include "core/luajit/value.hh"


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

  std::shared_ptr<Env> MakeDriversEnv(const std::shared_ptr<Env>&);
  Future<std::shared_ptr<luajit::Value>> MakeLuaExtension() noexcept;

 private:
  const std::shared_ptr<Impl> impl_;
};

}  // namespace nf7::core::imgui
