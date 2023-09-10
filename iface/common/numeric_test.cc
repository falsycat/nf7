// No copyright
#include "iface/common/numeric.hh"

#include <gtest/gtest.h>

#include "iface/common/exception.hh"


TEST(Numeric, CastSafelyShrink) {
  EXPECT_EQ(nf7::CastSafely<uint8_t>(uint32_t {0}), 0);
}
TEST(Numeric, CastSafelyShrinkWithOverflow) {
  EXPECT_THROW(nf7::CastSafely<uint8_t>(uint32_t {1000}), nf7::Exception);
}
TEST(Numeric, CastSafelyShrinkWithUnderflow) {
  EXPECT_THROW(nf7::CastSafely<int8_t>(int32_t {-1000}), nf7::Exception);
}
TEST(Numeric, CastSafelyShrinkWithSignDrop) {
  EXPECT_THROW(nf7::CastSafely<uint8_t>(int32_t {-1}), nf7::Exception);
}
TEST(Numeric, CastSafelyExpand) {
  EXPECT_EQ(nf7::CastSafely<uint32_t>(uint8_t {255}), 255);
}
