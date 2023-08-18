// No copyright
#pragma once

#include "iface/common/container.hh"
#include "iface/data/interface.hh"

namespace nf7 {

class File : public Container<data::Interface> {
 public:
  File() = default;

 public:
  Mutex& mtx() const noexcept { return mtx_; }

 public:
  using Container<data::Interface>::Get;
  using Container<data::Interface>::GetOr;
  using Container<data::Interface>::installed;

 private:
  mutable Mutex mtx_;
};

}  // namespace nf7
