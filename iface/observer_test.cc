// No copyright
#include "iface/observer.hh"
#include "iface/observer_test.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

using namespace nf7::iface;
using namespace nf7::iface::test;

TEST(Observer, NotifyWithMove) {
  ObserverTargetMock<int32_t> target;
  ObserverMock<int32_t> sut {target};

  EXPECT_CALL(sut, NotifyWithMove(111)).Times(1);

  target.Notify(int32_t {111});
}

TEST(Observer, NotifyWithRef) {
  ObserverTargetMock<int32_t> target;
  ObserverMock<int32_t> sut1 {target};
  ObserverMock<int32_t> sut2 {target};

  EXPECT_CALL(sut1, Notify(111)).Times(1);
  EXPECT_CALL(sut2, Notify(111)).Times(1);

  target.Notify(int32_t {111});
}

TEST(Observer, NotifyDestruction) {
  std::optional<ObserverTargetMock<int32_t>> target;
  target.emplace();
  ObserverMock<int32_t> sut {*target};

  EXPECT_CALL(sut, NotifyDestruction(testing::_)).Times(1);

  target = std::nullopt;
}

#if !defined(NDEBUG)
TEST(Observer, DeathByRegisterInCallback) {
  nf7::test::ObserverTargetMock<int32_t> target;
  nf7::test::ObserverMock<int32_t> sut {target};

  ON_CALL(sut, NotifyWithMove(testing::_)).WillByDefault([&](auto&&) {
    nf7::test::ObserverMock<int32_t> sut2 {target};
    (void) sut2;
  });

  ASSERT_DEATH_IF_SUPPORTED(target.Notify(111), "");
}
TEST(Observer, DeathByUnregisterInCallback) {
  nf7::test::ObserverTargetMock<int32_t> target;
  std::optional<nf7::test::ObserverMock<int32_t>> sut;
  sut.emplace(target);

  ON_CALL(*sut, NotifyWithMove(testing::_)).WillByDefault([&](auto&&) {
    sut = std::nullopt;
  });

  ASSERT_DEATH_IF_SUPPORTED(target.Notify(111), "");
}
TEST(Observer, DeathByNotifyInCallback) {
  nf7::test::ObserverTargetMock<int32_t> target;
  nf7::test::ObserverMock<int32_t> sut {{target}};

  ON_CALL(sut, NotifyWithMove(testing::_)).WillByDefault([&](auto&&) {
    target.Notify(111);
  });

  ASSERT_DEATH_IF_SUPPORTED(target.Notify(111), "");
}
#endif
