// No copyright
#include "core/gl3/context.hh"
#include "core/gl3/context_test.hh"

#include <gtest/gtest.h>

#include "iface/subsys/clock.hh"
#include "iface/subsys/concurrency.hh"


using namespace std::literals;


using Gl3Context = nf7::core::gl3::test::ContextFixture;

TEST_F(Gl3Context, Initialization) {
  env().Get<nf7::core::gl3::Context>();
  env().Get<nf7::subsys::Concurrency>()->Push(nf7::SyncTask {
    env().Get<nf7::subsys::Clock>()->now()+1000ms,
    [&](auto&) { DropEnv(); },
  });
  ConsumeTasks();
}
