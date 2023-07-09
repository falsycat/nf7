// No copyright
#pragma once

#include "iface/observer.hh"

#include <gmock/gmock.h>


namespace nf7::iface::test {

template <typename T>
class ObserverMock : public Observer<T> {
 public:
  explicit ObserverMock(Observer<T>::Target& target) : Observer<T>(target) {
  }

  MOCK_METHOD1(Notify, void(const T&));
  MOCK_METHOD1(NotifyWithMove, void(T&&));
  MOCK_METHOD1(NotifyDestruction, void(const T*));
};

template <typename T>
class ObserverTargetMock : public Observer<T>::Target {
 public:
  using Observer<T>::Target::Target;
  using Observer<T>::Target::Notify;
};

}  // namespace nf7::iface::test
