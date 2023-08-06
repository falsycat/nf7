// No copyright
#pragma once

#include "iface/subsys/clock.hh"

namespace nf7::core {

class Clock : public subsys::Clock {
 public:
  static Time GetCurrentTime() noexcept {
    return std::chrono::time_point_cast<
        Resolution>(std::chrono::system_clock::now());
  }

 public:
  explicit Clock(Time now = GetCurrentTime()) noexcept
      : subsys::Clock("Clock"), now_(now) { }

  void Tick(Time now = GetCurrentTime()) noexcept { now_ = now; }

  Time now() const noexcept { return now_; }

 private:
  Time now_;
};

}  // namespace nf7::core
