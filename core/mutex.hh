// No copyright
#pragma once

#include "iface/common/mutex.hh"
#include "iface/data/wrap.hh"

namespace nf7::core {

class Mutex : public data::Wrap<nf7::Mutex> {
 public:
  Mutex() : Wrap("nf7::core::Mutex", mtx_) { }

 private:
  nf7::Mutex mtx_;
};

}  // namespace nf7::core
