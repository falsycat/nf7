// No copyright
#include "iface/common/future.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "iface/common/exception.hh"

#include "iface/common/task_test.hh"


using namespace std::literals;


namespace {

enum FutureState {
  kYet,
  kDone,
  kError,
};

class FutureChain :
    public ::testing::TestWithParam<std::tuple<FutureState, FutureState>> {
 public:
  static std::string GenerateParamName(
      const ::testing::TestParamInfo<FutureChain::ParamType>& info) {
    const auto [s1, s2] = info.param;
    static const auto Stringify = [](auto s) {
      return s == kYet? "Yet": s == kDone? "Done": "Error";
    };
    return Stringify(s1) + "_"s + Stringify(s2);
  }

 protected:
  void SetUp() override {
    static const auto Prepare = [](auto& comp, FutureState state) {
      switch (state) {
      case kYet:
        return;
      case kDone:
        comp.Complete(int32_t {666});
        return;
      case kError:
        comp.Throw(nf7::Exception::MakePtr("helloworld"));
        return;
      }
    };
    const auto [s1, s2] = GetParam();
    Prepare(primary_, s1);
    Prepare(secondary_, s2);
  }

 protected:
  void TestSecondary(nf7::Future<int32_t> secondary) const {
    const auto [s1, s2] = GetParam();
    if (s1 == kDone && s2 == kDone) {
      EXPECT_TRUE(secondary.done());
    } else if (s1 == kError || (s1 != kYet && s2 == kError)) {
      EXPECT_TRUE(secondary.error());
    } else {
      EXPECT_TRUE(secondary.yet());
    }
  }

 protected:
  nf7::Future<int32_t>::Completer primary_;
  nf7::Future<int32_t>::Completer secondary_;
};

using FutureChainLazyAndLazy = FutureChain;
INSTANTIATE_TEST_SUITE_P(
    LazyAndLazy,
    FutureChainLazyAndLazy,
    testing::Combine(
        testing::Values(kYet, kDone, kError),
        testing::Values(kYet, kDone, kError)),
    FutureChain::GenerateParamName);

using FutureChainLazyAndImm = FutureChain;
INSTANTIATE_TEST_SUITE_P(
    LazyAndImm,
    FutureChainLazyAndImm,
    testing::Combine(
        testing::Values(kYet, kDone, kError),
        testing::Values(kDone, kError)),
    FutureChain::GenerateParamName);

}  // namespace


TEST(Future, ImmediateValue) {
  nf7::Future<int32_t> sut {int32_t {777}};

  EXPECT_FALSE(sut.yet());
  EXPECT_TRUE(sut.done());
  EXPECT_FALSE(sut.error());
  EXPECT_EQ(sut.value(), int32_t {777});
}
TEST(Future, ImmediateError) {
  nf7::Future<int32_t> sut {std::make_exception_ptr(nf7::Exception("hello"))};

  EXPECT_FALSE(sut.yet());
  EXPECT_FALSE(sut.done());
  EXPECT_TRUE(sut.error());
  EXPECT_THROW(std::rethrow_exception(sut.error()), nf7::Exception);
  EXPECT_THROW(sut.value(), nf7::Exception);
}

TEST(Future, LazyComplete) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  completer.Complete(int32_t {777});

  EXPECT_FALSE(sut.yet());
  EXPECT_TRUE(sut.done());
  EXPECT_FALSE(sut.error());
  EXPECT_EQ(sut.value(), int32_t {777});
}
TEST(Future, LazyThrow) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  completer.Throw(std::make_exception_ptr(nf7::Exception {"hello"}));

  EXPECT_FALSE(sut.yet());
  EXPECT_FALSE(sut.done());
  EXPECT_TRUE(sut.error());
  EXPECT_THROW(std::rethrow_exception(sut.error()), nf7::Exception);
  EXPECT_THROW(sut.value(), nf7::Exception);
}
TEST(Future, LazyIncomplete) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  EXPECT_TRUE(sut.yet());
  EXPECT_FALSE(sut.done());
  EXPECT_FALSE(sut.error());
}
TEST(Future, LazyForgotten) {
  std::optional<nf7::Future<int32_t>> sut;
  {
    std::optional<nf7::Future<int32_t>::Completer> completer;
    nf7::Future<int32_t> sut2 {*sut};
    {
      nf7::Future<int32_t>::Completer completer2;
      sut.emplace(completer2.future());
      completer.emplace(completer2);
    }
    EXPECT_TRUE(sut->yet());
    EXPECT_FALSE(sut->done());
    EXPECT_FALSE(sut->error());
  }
  EXPECT_FALSE(sut->yet());
  EXPECT_FALSE(sut->done());
  EXPECT_TRUE(sut->error());
}

TEST(Future, ListenImmediateValue) {
  nf7::Future<int32_t> sut {int32_t {777}};

  auto called = int32_t {0};
  sut.Listen([&](auto& fu) {
    ++called;
    EXPECT_EQ(fu.value(), int32_t {777});
  });

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenImmediateError) {
  nf7::Future<int32_t> sut {std::make_exception_ptr(nf7::Exception {"hello"})};

  auto called = int32_t {0};
  sut.Listen([&](auto& fu) {
    ++called;
    EXPECT_THROW(fu.value(), nf7::Exception);
  });

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenLazyComplete) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  auto called = int32_t {0};
  sut.Listen([&](auto& fu) {
    ++called;
    EXPECT_EQ(fu.value(), int32_t {777});
  });
  completer.Complete(int32_t {777});

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenLazyThrow) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  auto called = int32_t {0};
  sut.Listen([&](auto& fu) {
    ++called;
    EXPECT_THROW(fu.value(), nf7::Exception);
  });
  completer.Throw(std::make_exception_ptr(nf7::Exception {"hello"}));

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenLazyIncomplete) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  auto called = int32_t {0};
  sut.Listen([&](auto&) { ++called; });

  EXPECT_EQ(called, 0);
}
TEST(Future, ListenLazyForgotten) {
  auto called = int32_t {0};
  {
    nf7::Future<int32_t>::Completer completer;
    nf7::Future<int32_t> sut = completer.future();

    sut.Listen([&](auto& fu) {
      ++called;
      EXPECT_THROW(fu.value(), nf7::Exception);
    });
  }
  EXPECT_EQ(called, 1);
}

TEST(Future, AttachWhenYet) {
  nf7::Future<int32_t>::Completer comp;

  auto ptr = std::make_shared<int32_t>(0);
  comp.future().Attach(ptr);

  auto wptr = std::weak_ptr<int32_t> {ptr};
  ptr = nullptr;

  EXPECT_TRUE(wptr.lock());
}
TEST(Future, AttachWhenDone) {
  nf7::Future<int32_t> fu {0};

  auto ptr  = std::make_shared<int32_t>(0);
  auto wptr = std::weak_ptr<int32_t> {ptr};

  fu.Attach(ptr);
  ptr = nullptr;

  EXPECT_FALSE(wptr.lock());
}

TEST(Future, ThenWhenDone) {
  nf7::Future<int32_t> sut {int32_t {777}};

  auto called = int32_t {0};
  sut.Then([&](auto& x) {
    ++called;
    EXPECT_EQ(x, int32_t {777});
  });

  EXPECT_EQ(called, 1);
}
TEST(Future, ThenWhenError) {
  nf7::Future<int32_t> sut {std::make_exception_ptr(nf7::Exception {"hello"})};

  auto called = int32_t {0};
  sut.Then([&](auto&) { ++called; });

  EXPECT_EQ(called, 0);
}

TEST(Future, CatchWhenDone) {
  nf7::Future<int32_t> sut {int32_t {777}};

  auto called = int32_t {0};
  sut.Catch([&](auto&) { ++called; });

  EXPECT_EQ(called, int32_t {0});
}
TEST(Future, CatchWhenError) {
  nf7::Future<int32_t> sut {std::make_exception_ptr(nf7::Exception {"hello"})};

  auto called = int32_t {0};
  sut.Catch([&](auto&) { ++called; });

  EXPECT_EQ(called, 1);
}

TEST_P(FutureChainLazyAndImm, ThenAndWithValue) {
  const auto secondary = std::get<1>(GetParam());

  auto sut = primary_.future();
  TestSecondary(sut.ThenAnd([&](auto&) {
    switch (secondary) {
    case kError: throw nf7::Exception {"hello"};
    case kDone: return int32_t {666};
    default: assert(false); std::abort();
    }
  }));
}
TEST_P(FutureChainLazyAndLazy, ThenAndWithFuture) {
  auto sut = primary_.future();
  TestSecondary(sut.ThenAnd([&](auto&) { return secondary_.future(); }));
}

TEST(FutureCompleter, CompleteAfterCopy) {
  std::optional<nf7::Future<int32_t>> fut;
  {
    std::optional<nf7::Future<int32_t>::Completer> sut;
    {
      nf7::Future<int32_t>::Completer sut2;
      fut.emplace(sut2.future());
      sut.emplace(sut2);
    }
    sut->Complete(int32_t {777});
  }
  EXPECT_TRUE(fut->done());
}
TEST(FutureCompleter, CompleteAfterMove) {
  std::optional<nf7::Future<int32_t>> fut;
  {
    std::optional<nf7::Future<int32_t>::Completer> sut;
    {
      nf7::Future<int32_t>::Completer sut2;
      fut.emplace(sut2.future());
      sut.emplace(std::move(sut2));
    }
    sut->Complete(int32_t {777});
  }
  EXPECT_TRUE(fut->done());
}

TEST(FutureCompleter, RunWithComplete) {
  nf7::Future<int32_t>::Completer sut;
  sut.Run([]() { return int32_t {555}; });
  EXPECT_TRUE(sut.future().done());
}
TEST(FutureCompleter, RunWithThrow) {
  nf7::Future<int32_t>::Completer sut;
  sut.Run([]() -> int32_t { throw nf7::Exception {"helloworld"}; });
  EXPECT_TRUE(sut.future().error());
}

TEST(FutureCompleter, RunAsyncWithComplete) {
  nf7::Future<int32_t>::Completer sut;
  const auto fu = sut.future();

  const auto aq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::AsyncTask>>();
  const auto sq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::SyncTask>>();

  auto step = uint32_t {0};
  EXPECT_CALL(*aq_mock, Push)
      .WillOnce([&](auto&& task) {
        ++step;
        EXPECT_EQ(step, 1);

        nf7::AsyncTaskContext ctx;
        task(ctx);

        ++step;
        EXPECT_EQ(step, 3);
      });
  EXPECT_CALL(*sq_mock, Push)
      .WillOnce([&](auto&& task) {
        ++step;
        EXPECT_EQ(step, 2);

        nf7::SyncTaskContext ctx;
        EXPECT_TRUE(fu.yet());
        task(ctx);
        EXPECT_TRUE(fu.done());
      });

  sut.RunAsync(aq_mock, sq_mock, [](auto&) { return int32_t {777}; });
}

TEST(FutureCompleter, RunAsyncWithThrow) {
  nf7::Future<int32_t>::Completer sut;
  const auto fu = sut.future();

  const auto aq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::AsyncTask>>();
  const auto sq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::SyncTask>>();

  auto step = uint32_t {0};
  EXPECT_CALL(*aq_mock, Push)
      .WillOnce([&](auto&& task) {
        ++step;
        EXPECT_EQ(step, 1);

        nf7::AsyncTaskContext ctx;
        task(ctx);

        ++step;
        EXPECT_EQ(step, 3);
      });
  EXPECT_CALL(*sq_mock, Push)
      .WillOnce([&](auto&& task) {
        ++step;
        EXPECT_EQ(step, 2);

        nf7::SyncTaskContext ctx;
        EXPECT_TRUE(fu.yet());
        task(ctx);
        EXPECT_TRUE(fu.error());
      });

  sut.RunAsync(aq_mock, sq_mock,
               [](auto&) -> int32_t { throw nf7::Exception {"helloworld"}; });
}


#if !defined(NDEBUG)
TEST(Future, DeathByListenInCallback) {
  nf7::Future<int32_t> sut {int32_t{777}};

  sut.Listen([&](auto&) {
    ASSERT_DEATH_IF_SUPPORTED(sut.Listen([](auto&){}), "");
  });
}
TEST(Future, DeathByListenInLazyCallback) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  sut.Listen([&](auto&) {
    ASSERT_DEATH_IF_SUPPORTED(sut.Listen([](auto&){}), "");
  });

  completer.Complete(int32_t{777});
}
#endif
