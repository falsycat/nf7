// No copyright
#include "core/luajit/value.hh"

#include <gtest/gtest.h>

#include <cstring>

#include "core/luajit/context_test.hh"


namespace {
std::span<const uint8_t> toSpan(const char* v) noexcept {
  const auto ptr = reinterpret_cast<const uint8_t*>(v);
  return {ptr, ptr+std::strlen(v)};
}
std::vector<uint8_t> toVector(const char* v) noexcept {
  const auto sp = toSpan(v);
  return {sp.begin(), sp.end()};
}
}  // namespace


using LuaJIT_Value = nf7::core::luajit::test::ContextFixture;

TEST_P(LuaJIT_Value, MakeFunctionImmediately) {
  const auto ctx = env().Get<nf7::core::luajit::Context>();
  ctx->Exec([&](auto& ctx) {
    nf7::core::luajit::Value::MakeFunction(ctx, toSpan("local x = 1"), "hello");
  });
  ConsumeTasks();
}

TEST_P(LuaJIT_Value, MakeFunctionLater) {
  auto fu = nf7::core::luajit::Value::
      MakeFunction(env(), toVector("local x = 1"), "hello");

  ConsumeTasks();
  EXPECT_TRUE(fu.done());
}

TEST_P(LuaJIT_Value, MakeFunctionWithError) {
  auto fu = nf7::core::luajit::Value::
      MakeFunction(env(), toVector("hello world = 123"), "hello");

  ConsumeTasks();
  EXPECT_TRUE(fu.error());
}


INSTANTIATE_TEST_SUITE_P(
    SyncOrAsync, LuaJIT_Value,
    testing::Values(
        nf7::core::luajit::Context::kSync,
        nf7::core::luajit::Context::kAsync));
