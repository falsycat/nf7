// No copyright
#pragma once

#include "core/uv/context.hh"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

#include "iface/subsys/clock.hh"
#include "iface/env.hh"

#include "core/uv/clock.hh"


namespace nf7::core::uv::test {

class ContextFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    env_.emplace(SimpleEnv::FactoryMap {
      SimpleEnv::MakePair<Context, MainContext>(),
      SimpleEnv::MakePair<subsys::Clock, Clock>(),
    });
    ctx_ = std::dynamic_pointer_cast<MainContext>(env_->Get<Context>());
  }
  void TearDown() override {
    ctx_->RunAndClose();
    env_ = std::nullopt;
    ctx_ = nullptr;
  }

 protected:
  std::optional<SimpleEnv> env_;
  std::shared_ptr<MainContext> ctx_;
};

}  // namespace nf7::core:uv::test
