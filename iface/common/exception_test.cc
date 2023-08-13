// No copyright
#include "iface/common/exception.hh"

#include <gtest/gtest.h>

#include <string>


TEST(Exception, WithStaticMessage) {
  static const char* kMsg = "helloworld";
  nf7::Exception sut {kMsg};
  EXPECT_EQ(sut.what(), kMsg);
}

TEST(Exception, WithDynamicMessage) {
  static const char* kMsg = "helloworld";
  nf7::Exception sut {std::string {kMsg}};
  EXPECT_STREQ(sut.what(), kMsg);
  EXPECT_NE(sut.what(), kMsg);
}
