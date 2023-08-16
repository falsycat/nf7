// No copyright
#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "iface/subsys/clock.hh"
#include "iface/env.hh"

#include "core/uv/context.hh"
#include "core/clock.hh"

namespace nf7::core::uv {

class Clock : public subsys::Clock {
 public:
  explicit Clock(Env& env)
      : subsys::Clock("nf7::core::uv::Clock"),
        ctx_(env.Get<Context>()) { }

 public:
  void Reset(Time now = core::Clock::GetCurrentTime()) noexcept {
    epoch_ = now - std::chrono::milliseconds(ctx_->loop()->now());
  }

  Time now() const noexcept override {
    if (std::nullopt == epoch_) {
      const_cast<Clock&>(*this).Reset();
    }
    return *epoch_ + std::chrono::milliseconds(ctx_->loop()->now());
  }

 private:
  const std::shared_ptr<Context> ctx_;

  std::optional<Time> epoch_;
};

}  // namespace nf7::core::uv
