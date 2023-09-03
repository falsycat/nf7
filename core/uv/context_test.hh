// No copyright
#pragma once

#include "core/uv/context.hh"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

#include "iface/subsys/clock.hh"
#include "iface/env.hh"

#include "core/uv/clock.hh"

#include "iface/env_test.hh"


namespace nf7::core::uv::test {

class ContextFixture : public nf7::test::EnvFixture {
 public:
  ContextFixture() noexcept
      : nf7::test::EnvFixture({
            SimpleEnv::MakePair<Context, MainContext>(),
            SimpleEnv::MakePair<subsys::Clock, Clock>(),
          }) {
  }

 protected:
  void SetUp() override {
    nf7::test::EnvFixture::SetUp();
    ctx_ = std::dynamic_pointer_cast<MainContext>(env().Get<Context>());
  }
  void TearDown() override {
    ctx_->RunAndClose();
    nf7::test::EnvFixture::TearDown();
    ctx_ = nullptr;
  }

 protected:
  std::shared_ptr<MainContext> ctx_;
};

}  // namespace nf7::core:uv::test
