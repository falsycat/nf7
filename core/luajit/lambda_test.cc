// No copyright
#include "core/luajit/lambda.hh"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "iface/subsys/clock.hh"
#include "iface/subsys/dealer.hh"

#include "core/luajit/context.hh"
#include "core/clock.hh"
#include "core/dealer.hh"

#include "iface/common/observer_test.hh"
#include "iface/subsys/logger_test.hh"

#include "core/luajit/context_test.hh"


using namespace std::literals;

namespace {
class LuaJIT_Lambda : public nf7::core::luajit::test::ContextFixture {
 public:
  LuaJIT_Lambda()
      : maker_(std::make_shared<nf7::core::Maker<nf7::Value>>("mock maker")),
        taker_(std::make_shared<nf7::core::Taker<nf7::Value>>("mock taker")) {
    Install<nf7::subsys::Maker<nf7::Value>>(maker_);
    Install<nf7::subsys::Taker<nf7::Value>>(taker_);
  }

  std::shared_ptr<nf7::core::luajit::Value> Compile(
      const char* script) {
    auto lua = env().Get<nf7::core::luajit::Context>();

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
              const std::vector<nf7::Value>& out = {},
              nf7::Env* env = nullptr) {
    if (nullptr == env) {
      env = &this->env();
    }

    auto func = Compile(script);

    ::testing::StrictMock<
        nf7::test::ObserverMock<nf7::Value>> obs {*taker_};
    ::testing::Sequence seq;
    for (const auto& v : out) {
      EXPECT_CALL(obs, NotifyWithMove(nf7::Value {v}))
          .InSequence(seq);
    }

    auto sut = std::make_shared<nf7::core::luajit::Lambda>(*env, func);
    for (const auto& v : in) {
      maker_->Notify({v});
    }

    ConsumeTasks();
    EXPECT_EQ(sut->exitCount(), expectExit);
    EXPECT_EQ(sut->abortCount(), expectAbort);
  }

 protected:
  const std::shared_ptr<nf7::core::Maker<nf7::Value>> maker_;
  const std::shared_ptr<nf7::core::Taker<nf7::Value>> taker_;
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

  auto sut = std::make_shared<nf7::core::luajit::Lambda>(env(), func);
  maker_->Notify(nf7::Value::Null {});
  ConsumeTasks();
  EXPECT_EQ(sut->exitCount(), 0);
  EXPECT_EQ(sut->abortCount(), 0);

  maker_->Notify(nf7::Value::Integer {});
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
  const auto clock = std::make_shared<nf7::core::Clock>();
  nf7::SimpleEnv env {{
    {typeid(nf7::subsys::Clock), [&](auto&) { return clock; }},
  }, this->env().self()};

  clock->Tick();
  const auto begin = clock->now();

  Expect(
      "local ctx = ...\nctx:sleep(100)",
      {nf7::Value {}},
      1, 0,
      {},
      &env);

  clock->Tick();
  const auto end = clock->now();

  EXPECT_GE(end-begin, 100ms);
}

TEST_P(LuaJIT_Lambda, CtxLogging) {
  const auto logger = std::make_shared<nf7::subsys::test::LoggerMock>();

  EXPECT_CALL(*logger, Push)
      .WillOnce([](auto& item) {
        EXPECT_EQ(item.level(), nf7::subsys::Logger::kTrace);
        EXPECT_EQ(item.contents(), "this is trace");
      })
      .WillOnce([](auto& item) {
        EXPECT_EQ(item.level(), nf7::subsys::Logger::kInfo);
        EXPECT_EQ(item.contents(), "this is info");
      })
      .WillOnce([](auto& item) {
        EXPECT_EQ(item.level(), nf7::subsys::Logger::kWarn);
        EXPECT_EQ(item.contents(), "this is warn");
      })
      .WillOnce([](auto& item) {
        EXPECT_EQ(item.level(), nf7::subsys::Logger::kError);
        EXPECT_EQ(item.contents(), "this is error");
      });

  nf7::SimpleEnv env {{
    {typeid(nf7::subsys::Logger), [&](auto&) { return logger; }},
  }, this->env().self()};

  Expect(
      "local ctx = ...\n"
      "ctx:trace(\"this is trace\")\n"
      "ctx:info(\"this is info\")\n"
      "ctx:warn(\"this is warn\")\n"
      "ctx:error(\"this is error\")",
      {nf7::Value {}},
      1, 0,
      {},
      &env);
}

INSTANTIATE_TEST_SUITE_P(
    SyncOrAsync, LuaJIT_Lambda,
    testing::Values(
        nf7::core::luajit::Context::kSync,
        nf7::core::luajit::Context::kAsync));
