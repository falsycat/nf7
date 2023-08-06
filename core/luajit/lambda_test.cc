// No copyright
#include "core/luajit/lambda.hh"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "core/luajit/context.hh"

#include "iface/common/observer_test.hh"

#include "core/luajit/context_test.hh"


using namespace std::literals;

namespace {
class LuaJIT_Lambda : public nf7::core::luajit::test::ContextFixture {
 public:
  using ContextFixture::ContextFixture;

  std::shared_ptr<nf7::core::luajit::Value> Compile(
      const char* script) noexcept {
    auto lua = env_->Get<nf7::core::luajit::Context>();

    std::shared_ptr<nf7::core::luajit::Value> func;
    lua->Exec([&](auto& lua) {
      luaL_loadstring(*lua, script);
      func = lua.Register();
    });
    ConsumeTasks();
    return func;
  }

  void Expect(const char* script,
              const std::vector<nf7::Value>& in,
              uint32_t expectExit = 1, uint32_t expectAbort = 0,
              const std::vector<nf7::Value>& out = {}) {
    auto func = Compile(script);

    auto sut = std::make_shared<nf7::core::luajit::Lambda>(*env_, func);
    for (const auto& v : in) {
      sut->taker()->Take(v);
    }

    ::testing::StrictMock<
        nf7::test::ObserverMock<nf7::Value>> obs {*sut->maker()};

    ::testing::Sequence seq;
    for (const auto& v : out) {
      EXPECT_CALL(obs, NotifyWithMove(nf7::Value {v}))
          .InSequence(seq);
    }

    ConsumeTasks();
    EXPECT_EQ(sut->exitCount(), expectExit);
    EXPECT_EQ(sut->abortCount(), expectAbort);
  }
};
}  // namespace


TEST_P(LuaJIT_Lambda, Run) {
  Expect("local ctx = ...", {nf7::Value {}});
}
TEST_P(LuaJIT_Lambda, RunWithMultiInputs) {
  Expect("local ctx = ...",
         {nf7::Value {}, nf7::Value {}, nf7::Value {}},
         3);
}

TEST_P(LuaJIT_Lambda, CtxRecv) {
  Expect(
      "local ctx = ...\nnf7:assert(\"integer\" == ctx:recv():type())",
      {nf7::Value::Integer {77}});
}
TEST_P(LuaJIT_Lambda, CtxRecvWithMultiInput) {
  Expect("local ctx = ...\nnf7:assert(\"null\" == ctx:recv():type())",
         {nf7::Value {}, nf7::Value {}, nf7::Value {}},
         3, 0);
}
TEST_P(LuaJIT_Lambda, CtxMultiRecv) {
  Expect("local ctx = ...\n"
         "nf7:assert(\"null\"    == ctx:recv():type())\n"
         "nf7:assert(\"integer\" == ctx:recv():type())",
         {nf7::Value::Null {}, nf7::Value::Integer {}});
}
TEST_P(LuaJIT_Lambda, CtxMultiRecvWithDelay) {
  auto func = Compile("local ctx = ...\n"
                      "nf7:assert(\"null\"    == ctx:recv():type())\n"
                      "nf7:assert(\"integer\" == ctx:recv():type())");

  auto sut = std::make_shared<nf7::core::luajit::Lambda>(*env_, func);
  sut->taker()->Take(nf7::Value::Null {});
  ConsumeTasks();
  EXPECT_EQ(sut->exitCount(), 0);
  EXPECT_EQ(sut->abortCount(), 0);

  sut->taker()->Take(nf7::Value::Integer {});
  ConsumeTasks();
  EXPECT_EQ(sut->exitCount(), 1);
  EXPECT_EQ(sut->abortCount(), 0);
}
TEST_P(LuaJIT_Lambda, CtxMultiRecvAbort) {
  Expect("local ctx = ...\n"
         "nf7:assert(\"null\"    == ctx:recv():type())\n"
         "nf7:assert(\"integer\" == ctx:recv():type())",
         {nf7::Value::Null {}},
         0, 0);
}

TEST_P(LuaJIT_Lambda, CtxSend) {
  Expect(
      "local ctx = ...\nctx:send(nf7:value())",
      {nf7::Value {}},
      1, 0,
      {nf7::Value {}});
}
TEST_P(LuaJIT_Lambda, CtxSendWithMultiInput) {
  Expect(
      "local ctx = ...\nctx:send(nf7:value())",
      {nf7::Value {}, nf7::Value {}, nf7::Value {}},
      3, 0,
      {nf7::Value {}, nf7::Value {}, nf7::Value {}});
}
TEST_P(LuaJIT_Lambda, CtxMultiSend) {
  Expect(
      "local ctx = ...\nctx:send(nf7:value())\nctx:send(nf7:value())",
      {nf7::Value {}},
      1, 0,
      {nf7::Value {}, nf7::Value {}});
}

TEST_P(LuaJIT_Lambda, CtxSleep) {
  clock_->Tick();
  const auto begin = clock_->now();

  Expect(
      "local ctx = ...\nctx:sleep(100)",
      {nf7::Value {}},
      1, 0);

  clock_->Tick();
  const auto end = clock_->now();

  EXPECT_GE(end-begin, 100ms);
}

INSTANTIATE_TEST_SUITE_P(
    SyncOrAsync, LuaJIT_Lambda,
    testing::Values(
        nf7::core::luajit::Context::kSync,
        nf7::core::luajit::Context::kAsync));
