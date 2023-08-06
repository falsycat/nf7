// No copyright
#pragma once

#include <chrono>

#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Clock : public Interface {
 public:
  using Resolution = std::chrono::milliseconds;
  using Time       = std::chrono::sys_time<Resolution>;

  using Interface::Interface;

  virtual Time now() const noexcept = 0;
};

}  // namespace nf7::subsys
