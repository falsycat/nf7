// No copyright
#include "iface/common/dealer.hh"

#include <gtest/gtest.h>

#include "iface/common/observer_test.hh"


TEST(Emitter, Emit) {
  nf7::Emitter<int32_t> sut {{"hello", "this is a maker"}};
  nf7::test::ObserverMock<int32_t> obs {sut};

  EXPECT_CALL(obs, NotifyWithMove(int32_t {777})).Times(1);

  sut.Emit(int32_t {777});
}
