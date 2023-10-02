// No copyright
#pragma once

#include <cassert>
#include <memory>

#include "iface/common/value.hh"


namespace nf7::core::luajit {

class Context;

class Value final : public nf7::Value::Data {
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
