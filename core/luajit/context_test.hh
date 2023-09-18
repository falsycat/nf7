// No copyright
#pragma once

#include "core/luajit/context.hh"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "iface/common/exception.hh"
#include "iface/common/task.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/parallelism.hh"
#include "iface/env.hh"

#include "core/env_test.hh"

namespace nf7::core::luajit::test {

class ContextFixture :
    public nf7::core::test::EnvFixtureWithTasking,
    public ::testing::WithParamInterface<Context::Kind> {
 public:
  ContextFixture() noexcept
      : EnvFixtureWithTasking({
              {
                typeid(Context), [](auto& env) {
                  return Context::Create(env, GetParam());
                },
              },
            }) { }
};

}  // namespace nf7::core::luajit::test
