// No copyright
#include "iface/lambda.hh"
#include "iface/lambda_test.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>


TEST(LambdaBase, TakeAndRun) {
  nf7::test::LambdaBaseMock sut;

  EXPECT_CALL(sut, Main);

  sut.taker()->Take(nf7::Value::Null {});
}
