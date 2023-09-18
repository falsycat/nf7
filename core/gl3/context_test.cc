// No copyright
#include "core/gl3/context.hh"

#include <gtest/gtest.h>

#include <chrono>

#include "iface/subsys/concurrency.hh"

#include "core/env_test.hh"


using namespace std::literals;


class Gl3Context : public nf7::core::test::EnvFixtureWithTasking {
 public:
  Gl3Context() noexcept : skip_(nullptr == std::getenv("NF7_TEST_GL3")) { }

 public:
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

TEST_F(Gl3Context, Initialization) {
  auto ctx = std::make_shared<nf7::core::gl3::Context>(env());

  env().Get<nf7::subsys::Concurrency>()->Push(nf7::SyncTask {
    std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now())+1000ms,
    [&](auto&) { ctx = nullptr; },
  });
  ConsumeTasks();
}
