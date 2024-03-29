// No copyright
#pragma once

#include "core/uv/context.hh"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

#include "iface/subsys/clock.hh"
#include "iface/env.hh"

#include "core/uv/clock.hh"

#include "core/env_test.hh"


namespace nf7::core::uv::test {

class ContextFixture : public nf7::core::test::EnvFixture {
 public:
  ContextFixture() {
    Install<Context, MainContext>();
    Install<subsys::Clock, Clock>();
  }

 protected:
  void SetUp() override {
    nf7::core::test::EnvFixture::SetUp();
    ctx_ = std::dynamic_pointer_cast<MainContext>(env().Get<Context>());
  }
  void TearDown() override {
    ctx_->RunAndClose();
    nf7::core::test::EnvFixture::TearDown();
    ctx_ = nullptr;
  }

 protected:
  std::shared_ptr<MainContext> ctx_;
};

}  // namespace nf7::core:uv::test
