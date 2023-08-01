// No copyright
#pragma once

#include "iface/lambda.hh"

#include <gmock/gmock.h>

#include "iface/common/dealer.hh"
#include "iface/common/value.hh"


namespace nf7::test {

class LambdaBaseMock : public LambdaBase {
 public:
  using LambdaBase::LambdaBase;

  MOCK_METHOD(void, Main, (const Value&), (noexcept));

  using LambdaBase::emitter;
};

}  // namespace nf7::test
