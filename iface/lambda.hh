// No copyright
#pragma once

#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "iface/common/dealer.hh"
#include "iface/common/value.hh"

namespace nf7 {

class Lambda {
 public:
  Lambda() = delete;
  Lambda(std::vector<std::shared_ptr<Taker<Value>>>&& takers,
         std::vector<std::shared_ptr<Maker<Value>>>&& makers) noexcept
      : takers_(std::move(takers)), makers_(makers) {
  }
  virtual ~Lambda() = default;

  Lambda(const Lambda&) = delete;
  Lambda(Lambda&&) = delete;
  Lambda& operator=(const Lambda&) = delete;
  Lambda& operator=(Lambda&&) = delete;

  std::span<const std::shared_ptr<Taker<Value>>> takers() const noexcept {
    return takers_;
  }
  std::span<const std::shared_ptr<Maker<Value>>> makers() const noexcept {
    return makers_;
  }

 private:
  std::vector<std::shared_ptr<Taker<Value>>> takers_;
  std::vector<std::shared_ptr<Maker<Value>>> makers_;
};

}  // namespace nf7
