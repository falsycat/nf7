// No copyright
#pragma once

#include "core/gl3/context.hh"

#include <gtest/gtest.h>

#include <memory>

#include "iface/env.hh"

#include "core/env_test.hh"


namespace nf7::core::gl3::test {

class ContextFixture : public nf7::core::test::EnvFixtureWithTasking {
 public:
  ContextFixture() noexcept
      : EnvFixtureWithTasking({
          nf7::SimpleEnv::MakePair<Context, Context>(),
        }),
        skip_(nullptr == std::getenv("NF7_TEST_GL3")) { }

 protected:
  void SetUp() override {
    if (skip_) {
      GTEST_SKIP();
    } else {
      nf7::core::test::EnvFixtureWithTasking::SetUp();
    }
  }
  void TearDown() override {
    if (!skip_) {
      nf7::core::test::EnvFixtureWithTasking::TearDown();
    }
  }

 private:
  const bool skip_;
};


}  // namespace nf7::core::gl3::test
