// No copyright
#pragma once

#include "iface/common/observer.hh"

#include <gmock/gmock.h>


namespace nf7::test {

template <typename T>
class ObserverMock : public Observer<T> {
 public:
  explicit ObserverMock(Observer<T>::Target& target) : Observer<T>(target) {
  }

  MOCK_METHOD(void, Notify, (const T&));
  MOCK_METHOD(void, NotifyWithMove, (T&&));
  MOCK_METHOD(void, NotifyDestruction, (const T*));
};

template <typename T>
class ObserverTargetMock : public Observer<T>::Target {
 public:
  using Observer<T>::Target::Target;
  using Observer<T>::Target::Notify;
};

}  // namespace nf7::test
