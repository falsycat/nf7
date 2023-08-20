// No copyright
#pragma once

#include "iface/data/interface.hh"

namespace nf7::data {

template <typename T>
class Wrap : public Interface {
 protected:
  Wrap(const char* name, T& data) noexcept
      : Interface(name), data_(data) { }

 public:
  T& data() const noexcept { return data_; }

 private:
  T& data_;
};

}  // namespace nf7::data
