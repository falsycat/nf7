// No copyright
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "iface/common/dealer.hh"
#include "iface/common/value.hh"

namespace nf7 {

class Lambda {
 public:
  Lambda() = delete;
  Lambda(std::shared_ptr<Taker<Value>>&& taker,
         std::shared_ptr<Maker<Value>>&& maker) noexcept
      : taker_(std::move(taker)), maker_(maker) {
  }
  virtual ~Lambda() = default;

  Lambda(const Lambda&) = delete;
  Lambda(Lambda&&) = delete;
  Lambda& operator=(const Lambda&) = delete;
  Lambda& operator=(Lambda&&) = delete;

  const std::shared_ptr<Taker<Value>>& taker() const noexcept { return taker_; }
  const std::shared_ptr<Maker<Value>>& maker() const noexcept { return maker_; }

 private:
  const std::shared_ptr<Taker<Value>> taker_;
  const std::shared_ptr<Maker<Value>> maker_;
};

}  // namespace nf7
