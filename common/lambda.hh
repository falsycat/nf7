#pragma once

#include <memory>

#include "nf7.hh"

#include "common/value.hh"


namespace nf7 {

class Lambda : public nf7::Context {
 public:
  using nf7::Context::Context;

  virtual void Init(const std::shared_ptr<Lambda>& parent) noexcept {
    (void) parent;
  }

  virtual void Handle(
      size_t idx, Value&&, const std::shared_ptr<Lambda>& sender) noexcept = 0;
};

}  // namespace nf7
