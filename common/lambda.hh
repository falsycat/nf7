#pragma once

#include <memory>

#include "common/value.hh"


namespace nf7 {

class Lambda {
 public:
  Lambda() = default;
  virtual ~Lambda() = default;
  Lambda(const Lambda&) = delete;
  Lambda(Lambda&&) = delete;
  Lambda& operator=(const Lambda&) = delete;
  Lambda& operator=(Lambda&&) = delete;

  virtual void Handle(
      size_t idx, Value&&, const std::shared_ptr<Lambda>& recv) noexcept = 0;
};

}  // namespace nf7
