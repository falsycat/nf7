// No copyright
#include "iface/common/future.hh"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>

#include "iface/common/exception.hh"


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
  sut.Then([&](auto& x) { ++called; });

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
  sut.Catch([&](auto& e) { ++called; });

  EXPECT_EQ(called, 1);
}

TEST(Future, ThenAndWhenDone) {
  nf7::Future<int32_t> sut {int32_t {777}};

  auto called1 = int32_t {0};
  auto called2 = int32_t {0};
  sut
    .ThenAnd<int32_t>([&](auto& x) {
      ++called1;
      EXPECT_EQ(x, int32_t {777});
      return int32_t {666};
    })
    .Then([&](auto& x) {
      ++called2;
      EXPECT_EQ(x, int32_t {666});
    });

  EXPECT_EQ(called1, 1);
  EXPECT_EQ(called2, 1);
}
TEST(Future, ThenAndWhenError) {
  nf7::Future<int32_t> sut {std::make_exception_ptr(nf7::Exception {"hello"})};

  auto called1 = int32_t {0};
  auto called2 = int32_t {0};
  sut
    .ThenAnd<int32_t>([&](auto&) {
      ++called1;
      return int32_t {666};
    })
    .Then([&](auto&) {
      ++called2;
    });

  EXPECT_EQ(called1, 0);
  EXPECT_EQ(called2, 0);
}

TEST(Future_Completer, CompleteAfterCopy) {
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
TEST(Future_Completer, CompleteAfterMove) {
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
