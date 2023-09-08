// No copyright
#pragma once

#include "iface/subsys/interface.hh"

namespace nf7::subsys {

template <typename T>
class Wrapper : public Interface {
 protected:
  Wrapper(const char* name, T& data) noexcept
      : Interface(name), data_(data) { }

 public:
  T& data() const noexcept { return data_; }

 private:
  T& data_;
};

}  // namespace nf7::subsys
