// No copyright
#include "core/luajit/context.hh"
#include "core/luajit/context_test.hh"


using LuaJIT_Context = nf7::core::luajit::test::ContextFixture;
using LuaJIT_Value   = nf7::core::luajit::test::ContextFixture;

TEST_P(LuaJIT_Context, CreateAndDestroy) {
  auto sut = env_->Get<nf7::core::luajit::Context>();
  EXPECT_EQ(sut->kind(), GetParam());
}
TEST_P(LuaJIT_Context, Register) {
  auto sut = env_->Get<nf7::core::luajit::Context>();
  sut->Exec([](auto& ctx) {
    lua_createtable(*ctx, 0, 0);
    auto value = ctx.Register();

    EXPECT_NE(value->index(), LUA_REFNIL);
    EXPECT_NE(value->index(), LUA_NOREF);
  });

  ConsumeTasks();
}
TEST_P(LuaJIT_Context, Query) {
  auto sut = env_->Get<nf7::core::luajit::Context>();

  std::shared_ptr<nf7::core::luajit::Value> value;

  sut->Exec([&](auto& ctx) {
    lua_pushstring(*ctx, "helloworld");
    value = ctx.Register();
  });
  sut->Exec([&](auto& ctx) {
    EXPECT_TRUE(nullptr != value);

    ctx.Query(*value);

    const char* str = lua_tostring(*ctx, -1);
    EXPECT_STREQ(str, "helloworld");
  });

  ConsumeTasks();
}

INSTANTIATE_TEST_SUITE_P(
    SyncOrAsync, LuaJIT_Context,
    testing::Values(
        nf7::core::luajit::Context::kSync,
        nf7::core::luajit::Context::kAsync));
