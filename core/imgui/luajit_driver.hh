// No copyright
#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "iface/common/future.hh"
#include "iface/common/value.hh"
#include "iface/subsys/dealer.hh"

#include "core/imgui/context.hh"
#include "core/imgui/driver.hh"
#include "core/luajit/context.hh"
#include "core/luajit/lambda.hh"
#include "core/dealer.hh"


namespace nf7::core::imgui {

class LuaJITDriver :
    public Driver,
    public std::enable_shared_from_this<LuaJITDriver> {
 public:
  static std::shared_ptr<luajit::Value>
      MakeExtensionObject(luajit::TaskContext&);

 public:
  static Future<std::shared_ptr<Driver>> CompileAndInstall(
      Env&, std::vector<uint8_t>&& script, std::string_view name) noexcept;

 public:
  explicit LuaJITDriver(
      const std::shared_ptr<Maker<nf7::Value>>& maker,
      const std::shared_ptr<luajit::Value>&     ext,
      const std::shared_ptr<luajit::Lambda>&    la)
      : maker_(maker), ext_(ext), la_(la) { }

 private:
  void Update(gl3::TaskContext&) noexcept override {
    maker_->Notify(ext_);
  }

 private:
  const std::shared_ptr<Maker<nf7::Value>> maker_;
  const std::shared_ptr<luajit::Value>     ext_;
  const std::shared_ptr<Lambda>            la_;
};

}  // namespace nf7::core::imgui
