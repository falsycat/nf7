// No copyright
#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "iface/common/value.hh"
#include "iface/common/future.hh"
#include "iface/env.hh"


namespace nf7::core::luajit {

class Context;
class TaskContext;

class Value final : public nf7::Value::Data {
 public:
  static std::shared_ptr<Value> MakeFunction(
      TaskContext&,
      std::span<const uint8_t>,
      const char* name = "");
  static Future<std::shared_ptr<Value>> MakeFunction(
      Env&,
      std::vector<uint8_t>&&,
      std::string name = "") noexcept;

 public:
  Value() = delete;
  Value(const std::shared_ptr<Context>& ctx, int index) noexcept
      : ctx_(ctx), index_(index) {
    assert(nullptr != ctx_);
  }
  ~Value() noexcept override;

 public:
  Value(const Value&) = delete;
  Value(Value&&) = delete;
  Value& operator=(const Value&) = delete;
  Value& operator=(Value&&) = delete;

 public:
  const std::shared_ptr<Context>& context() const noexcept { return ctx_; }
  int index() const noexcept { return index_; }

 private:
  std::shared_ptr<Context> ctx_;
  int index_;
};

}  // namespace nf7::core::luajit
