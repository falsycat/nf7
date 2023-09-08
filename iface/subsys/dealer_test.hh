// No copyright
#pragma once

#include "iface/subsys/dealer.hh"

#include <gmock/gmock.h>


namespace nf7::subsys::test {

template <typename T>
class TakerMock : public Taker<T> {
 public:
  TakerMock() noexcept
      : Taker<T>("nf7::subsys::test::TakerMock") { }

  MOCK_METHOD(void, Take, (T&&), (noexcept, override));
};

}  // namespace nf7::subsys::test
