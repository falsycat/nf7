// No copyright
#include "iface/common/mutex.hh"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <vector>


using namespace std::literals;

TEST(Mutex, TryLock) {
  nf7::Mutex mtx;
  auto k = mtx.TryLock();
  EXPECT_TRUE(k);
}

TEST(Mutex, TryLockFails) {
  nf7::Mutex mtx;
  auto k1 = mtx.TryLock();
  auto k2 = mtx.TryLock();
  EXPECT_FALSE(k2);
}

TEST(Mutex, Lock) {
  nf7::Mutex mtx;
  auto fu = mtx.Lock();
  EXPECT_TRUE(fu.done());
}

TEST(Mutex, LockPending) {
  nf7::Mutex mtx;
  auto k  = mtx.TryLock();
  auto fu = mtx.Lock();
  EXPECT_TRUE(fu.yet());
}

TEST(Mutex, LockWithDelay) {
  nf7::Mutex mtx;
  auto k  = mtx.TryLock();
  auto fu = mtx.Lock();

  k = nullptr;
  EXPECT_TRUE(fu.done());
}

TEST(Mutex, LockAbort) {
  std::optional<nf7::Mutex> mtx;
  mtx.emplace();

  auto k  = mtx->TryLock();
  auto fu = mtx->Lock();

  mtx = std::nullopt;
  EXPECT_TRUE(fu.error());
}

TEST(Mutex, ChaoticLock) {
  nf7::Mutex mtx;
  std::atomic<bool> flag = false;

  std::vector<std::thread> threads;
  threads.resize(16);
  for (auto& th : threads) {
    th = std::thread {[&]() {
      mtx.Lock()
          .Then([&](auto&) {
            const auto flag_at_first = flag.exchange(true);
            EXPECT_FALSE(flag_at_first);

            std::this_thread::sleep_for(10ms);

            const auto flag_at_last = flag.exchange(false);
            EXPECT_TRUE(flag_at_last);
          })
          .Catch([&](const auto& e) {
            FAIL() << e;
          });
    }};
  }
  for (auto& th : threads) {
    th.join();
  }
}
