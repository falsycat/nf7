// No copyright
#include "core/clock.hh"

#include <gtest/gtest.h>

#include <chrono>


using namespace std::literals;

TEST(Clock, now) {
  nf7::core::Clock sut {nf7::core::Clock::Time {0ms}};
  EXPECT_EQ(sut.now(), nf7::core::Clock::Time {0ms});
}

TEST(Clock, Tick) {
  nf7::core::Clock sut {nf7::core::Clock::Time {0ms}};
  sut.Tick(nf7::core::Clock::Time {1ms});
  EXPECT_EQ(sut.now(), nf7::core::Clock::Time {1ms});
}
