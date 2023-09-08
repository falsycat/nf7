// No copyright
#pragma once

#include "iface/common/observer.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

template <typename T>
class Maker : public Interface, public Observer<T>::Target {
 public:
  using Interface::Interface;

 protected:
  using Observer<T>::Target::Notify;
};

template <typename T>
class Taker : public Interface {
 public:
  using Interface::Interface;

  virtual void Take(T&&) noexcept = 0;
};

}  // namespace nf7::subsys
