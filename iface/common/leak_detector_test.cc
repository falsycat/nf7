// No copyright
#include "iface/common/leak_detector.hh"

#include <gtest/gtest.h>

#include <memory>


namespace {
class A : private nf7::LeakDetector<A> { };
}  // namespace

#if !defined(NDEBUG)
TEST(LeakDetector, Counter) {
  {
    A a;
    EXPECT_EQ(nf7::LeakDetector<A>::count(), 1);
  }
  EXPECT_EQ(nf7::LeakDetector<A>::count(), 0);
}
#endif
