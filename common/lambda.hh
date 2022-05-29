#pragma once

#include <cassert>
#include <memory>

#include "nf7.hh"

#include "common/value.hh"


namespace nf7 {

class Lambda : public nf7::Context {
 public:
  using nf7::Context::Context;

  virtual void Initialize(const std::shared_ptr<Lambda>& self) {
    assert(self.get() == this);
    self_ = self;
  }

  virtual void Handle(
      size_t idx, Value&&, const std::shared_ptr<Lambda>& sender) = 0;

  std::shared_ptr<Lambda> self() const noexcept { return self_.lock(); }

 private:
  std::weak_ptr<Lambda> self_;
};

}  // namespace nf7
