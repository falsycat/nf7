// No copyright
#include "core/uv/concurrency.hh"

#include <gtest/gtest.h>

#include <chrono>

#include "iface/subsys/clock.hh"

#include "core/uv/context_test.hh"
#include "core/clock.hh"


using namespace std::literals;

using UV_Concurrency = nf7::core::uv::test::ContextFixture;


TEST_F(UV_Concurrency, Push) {
  auto sut = std::make_shared<nf7::core::uv::Concurrency>(*env_);

  auto called = uint64_t {0};
  sut->Exec([&](auto&) { ++called; });

  ctx_->Run();
  EXPECT_EQ(called, 1);
}

TEST_F(UV_Concurrency, PushFromTask) {
  auto sut = std::make_shared<nf7::core::uv::Concurrency>(*env_);

  auto called = uint64_t {0};
  sut->Exec([&](auto&) { sut->Exec([&](auto&) { ++called; }); });

  ctx_->Run();
  EXPECT_EQ(called, 1);
}

TEST_F(UV_Concurrency, PushWithDelay) {
  auto clock = env_->Get<nf7::subsys::Clock>();
  auto sut   = std::make_shared<nf7::core::uv::Concurrency>(*env_);

  auto called = uint64_t {0};
  sut->Push({clock->now() + 100ms, [&](auto&) { ++called; }});

  const auto begin = clock->now();
  ctx_->Run();
  const auto end = clock->now();

  EXPECT_EQ(called, 1);
  EXPECT_GE(end-begin, 100ms);
}
