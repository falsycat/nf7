// No copyright
#include "core/luajit/thread.hh"
#include "core/luajit/thread_test.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/luajit/context_test.hh"


class LuaJIT_Thread : public nf7::core::luajit::test::ContextFixture {
 public:
  using ContextFixture::ContextFixture;

  template <typename... Args>
  void TestThread(
      const auto& setup, const char* script, Args&&... args) {
    auto lua    = env_->Get<nf7::core::luajit::Context>();
    auto called = uint32_t {0};
    lua->Exec([&](auto& lua) {
      const auto compile = luaL_loadstring(*lua, script);
      ASSERT_EQ(compile, LUA_OK);

      auto sut = nf7::core::luajit::Thread::Make<
          nf7::core::luajit::test::ThreadMock>(lua, lua.Register());
      setup(*sut);

      sut->Resume(lua, std::forward<Args>(args)...);
      ++called;
    });
    ConsumeTasks();
    EXPECT_EQ(called, 1);
  }
};


TEST_P(LuaJIT_Thread, ResumeWithSingleReturn) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onExited)
      .WillOnce([](auto& lua) { EXPECT_EQ(lua_tointeger(*lua, 1), 6); });
  },
  "return 1+2+3");
}

TEST_P(LuaJIT_Thread, ResumeWithArgs) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onExited)
      .WillOnce([](auto& lua) { EXPECT_EQ(lua_tointeger(*lua, 1), 60); });
  },
  "local x,y,z = ...\nreturn x+y+z",
  lua_Integer {10}, lua_Integer {20}, lua_Integer {30});
}

TEST_P(LuaJIT_Thread, RunWithMultipleReturn) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onExited)
      .WillOnce([](auto& lua) {
        EXPECT_EQ(lua_gettop(*lua), 3);
        EXPECT_EQ(lua_tointeger(*lua, 1), 1);
        EXPECT_EQ(lua_tointeger(*lua, 2), 2);
        EXPECT_EQ(lua_tointeger(*lua, 3), 3);
      });
  },
  "return 1, 2, 3");
}

TEST_P(LuaJIT_Thread, RunAndError) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onAborted);
  },
  "return foo()");
}

TEST_P(LuaJIT_Thread, ForbidGlobalVariable) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onAborted)
      .WillOnce([](auto& lua) {
        EXPECT_THAT(lua_tostring(*lua, -1), ::testing::HasSubstr("immutable"));
      });
  },
  "x = 1");
}

TEST_P(LuaJIT_Thread, StdThrow) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onAborted)
      .WillOnce([](auto& lua) {
        EXPECT_THAT(lua_tostring(*lua, -1),
                    ::testing::HasSubstr("hello world"));
      });
  },
  "nf7:throw(\"hello world\")");
}

TEST_P(LuaJIT_Thread, StdAssertWithTrue) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onExited);
  },
  "nf7:assert(true)");
}
TEST_P(LuaJIT_Thread, StdAssertWithFalse) {
  TestThread([](auto& sut) {
    EXPECT_CALL(sut, onAborted);
  },
  "nf7:assert(false)");
}


INSTANTIATE_TEST_SUITE_P(
    SyncOrAsync, LuaJIT_Thread,
    testing::Values(
        nf7::core::luajit::Context::kSync,
        nf7::core::luajit::Context::kAsync));
