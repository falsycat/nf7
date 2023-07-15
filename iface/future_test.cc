// No copyright
#include "iface/future.hh"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "iface/exception.hh"


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
  nf7::Future<int32_t> sut2 {*sut};
  {
    nf7::Future<int32_t>::Completer completer;
    sut.emplace(completer.future());
  }
  EXPECT_FALSE(sut->yet());
  EXPECT_FALSE(sut->done());
  EXPECT_TRUE(sut->error());
}

TEST(Future, ListenImmediateValue) {
  nf7::Future<int32_t> sut {int32_t {777}};

  auto called = int32_t {0};
  sut.Listen([&]() {
    ++called;
    EXPECT_EQ(sut.value(), int32_t {777});
  });

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenImmediateError) {
  nf7::Future<int32_t> sut {std::make_exception_ptr(nf7::Exception {"hello"})};

  auto called = int32_t {0};
  sut.Listen([&]() {
    ++called;
    EXPECT_THROW(std::rethrow_exception(sut.error()), nf7::Exception);
  });

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenLazyComplete) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  auto called = int32_t {0};
  sut.Listen([&]() {
    ++called;
    EXPECT_EQ(sut.value(), int32_t {777});
  });
  completer.Complete(int32_t {777});

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenLazyThrow) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  auto called = int32_t {0};
  sut.Listen([&]() {
    ++called;
    EXPECT_THROW(std::rethrow_exception(sut.error()), nf7::Exception);
  });
  completer.Throw(std::make_exception_ptr(nf7::Exception {"hello"}));

  EXPECT_EQ(called, 1);
}
TEST(Future, ListenLazyIncomplete) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  auto called = int32_t {0};
  sut.Listen([&]() { ++called; });

  EXPECT_EQ(called, 0);
}
TEST(Future, ListenLazyForgotten) {
  auto called = int32_t {0};
  {
    nf7::Future<int32_t>::Completer completer;
    nf7::Future<int32_t> sut = completer.future();

    sut.Listen([&, sut]() {
      ++called;
      EXPECT_THROW(std::rethrow_exception(sut.error()), nf7::Exception);
    });
  }
  EXPECT_EQ(called, 1);
}

#if !defined(NDEBUG)
TEST(Future, DeathByListenInCallback) {
  nf7::Future<int32_t> sut {int32_t{777}};

  sut.Listen([&]() {
    ASSERT_DEATH_IF_SUPPORTED(sut.Listen([](){}), "");
  });
}
TEST(Future, DeathByListenInLazyCallback) {
  nf7::Future<int32_t>::Completer completer;
  nf7::Future<int32_t> sut = completer.future();

  sut.Listen([&]() {
    ASSERT_DEATH_IF_SUPPORTED(sut.Listen([](){}), "");
  });

  completer.Complete(int32_t{777});
}
#endif
