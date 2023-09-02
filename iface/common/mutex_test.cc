// No copyright
#include "iface/common/mutex.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include "iface/common/exception.hh"

#include "iface/common/task_test.hh"


using namespace std::literals;

TEST(Mutex, TryLockEx) {
  nf7::Mutex mtx;
  auto k = mtx.TryLockEx();
  EXPECT_TRUE(k);
}

TEST(Mutex, TryLockExFails) {
  nf7::Mutex mtx;
  auto k1 = mtx.TryLockEx();
  auto k2 = mtx.TryLockEx();
  EXPECT_FALSE(k2);
}

TEST(Mutex, TryLock) {
  nf7::Mutex mtx;
  auto k = mtx.TryLock();
  EXPECT_TRUE(k);
}

TEST(Mutex, TryLockWithShared) {
  nf7::Mutex mtx;
  auto k1 = mtx.TryLock();
  auto k2 = mtx.TryLock();
  EXPECT_TRUE(k2);
}

TEST(Mutex, TryLockFails) {
  nf7::Mutex mtx;
  auto k1 = mtx.TryLockEx();
  auto k2 = mtx.TryLock();
  EXPECT_FALSE(k2);
}

TEST(Mutex, LockEx) {
  nf7::Mutex mtx;
  auto fu = mtx.LockEx();
  EXPECT_TRUE(fu.done());
}

TEST(Mutex, LockWithShare) {
  nf7::Mutex mtx;
  auto fu1 = mtx.Lock();
  auto fu2 = mtx.Lock();
  EXPECT_TRUE(fu1.done());
  EXPECT_TRUE(fu2.done());
}

TEST(Mutex, LockExPending) {
  nf7::Mutex mtx;
  auto k  = mtx.TryLockEx();
  auto fu = mtx.LockEx();
  EXPECT_TRUE(fu.yet());
}

TEST(Mutex, LockPending) {
  nf7::Mutex mtx;
  auto k   = mtx.TryLockEx();
  auto fu1 = mtx.Lock();
  auto fu2 = mtx.Lock();
  EXPECT_TRUE(fu1.yet());
  EXPECT_TRUE(fu2.yet());
}

TEST(Mutex, LockExWithDelay) {
  nf7::Mutex mtx;
  auto k  = mtx.TryLockEx();
  auto fu = mtx.LockEx();
  k = nullptr;
  EXPECT_TRUE(fu.done());
}

TEST(Mutex, LockWithDelay) {
  nf7::Mutex mtx;
  auto k  = mtx.TryLockEx();
  auto fu1 = mtx.Lock();
  auto fu2 = mtx.Lock();
  k = nullptr;
  EXPECT_TRUE(fu1.done());
  EXPECT_TRUE(fu2.done());
}

TEST(Mutex, LockExAbort) {
  std::optional<nf7::Mutex> mtx;
  mtx.emplace();

  auto k  = mtx->TryLockEx();
  auto fu = mtx->LockEx();

  mtx = std::nullopt;
  EXPECT_TRUE(fu.error());
}

TEST(Mutex, LockAbort) {
  std::optional<nf7::Mutex> mtx;
  mtx.emplace();

  auto k  = mtx->TryLockEx();
  auto fu = mtx->Lock();

  mtx = std::nullopt;
  EXPECT_TRUE(fu.error());
}

TEST(Mutex, RunAsyncWithComplete) {
  nf7::Mutex mtx;

  const auto aq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::AsyncTask>>();
  const auto sq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::SyncTask>>();

  ON_CALL(*aq_mock, Push)
      .WillByDefault([&](auto&& task) {
        nf7::AsyncTaskContext ctx;
        task(ctx);
      });
  ON_CALL(*sq_mock, Push)
      .WillByDefault([&](auto&& task) {
        nf7::SyncTaskContext ctx;
        task(ctx);
      });

  auto fu = mtx.RunAsync(aq_mock, sq_mock, [](auto&) { return int32_t {777}; });
  EXPECT_TRUE(fu.done());
}
TEST(Mutex, RunAsyncWithError) {
  nf7::Mutex mtx;

  const auto aq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::AsyncTask>>();
  const auto sq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::SyncTask>>();

  ON_CALL(*aq_mock, Push)
      .WillByDefault([&](auto&& task) {
        nf7::AsyncTaskContext ctx;
        task(ctx);
      });
  ON_CALL(*sq_mock, Push)
      .WillByDefault([&](auto&& task) {
        nf7::SyncTaskContext ctx;
        task(ctx);
      });

  auto fu = mtx.RunAsync(aq_mock, sq_mock, [](auto&) -> int32_t {
    throw nf7::Exception {"helloworld"};
  });
  EXPECT_TRUE(fu.error());
}
TEST(Mutex, RunAsyncWithAbort) {
  std::optional<nf7::Mutex> mtx;
  mtx.emplace();

  const auto aq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::AsyncTask>>();
  const auto sq_mock =
      std::make_shared<nf7::test::TaskQueueMock<nf7::SyncTask>>();

  auto fu = mtx->RunAsync(aq_mock, sq_mock, [](auto&) { return 0; });
  mtx = std::nullopt;
  EXPECT_TRUE(fu.error());
}
