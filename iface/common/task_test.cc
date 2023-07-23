// No copyright
#include "iface/common/task.hh"
#include "iface/common/task_test.hh"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

#include "iface/common/future.hh"

using namespace std::literals;


TEST(Task, ExecAndThrow) {
  const auto line = __LINE__ + 1;
  nf7::Task<> task {[&]() { throw nf7::Exception {"hello"}; }};

  try {
    task();
    EXPECT_FALSE("unreachable (exception expected)");
  } catch (const nf7::Exception& e) {
    EXPECT_EQ(e.location().line(), line);
    EXPECT_EQ(e.location().file_name(), __FILE__);
  }
}

TEST(Task, ExecWithParam) {
  auto called = uint32_t {0};
  nf7::Task<uint32_t, uint32_t> task {[&](auto x, auto y) {
    ++called;
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 1);
  }};
  task(0, 1);

  EXPECT_EQ(called, 1);
}

TEST(TaskQueue, WrapLambda) {
  auto sut     = std::make_shared<nf7::test::TaskQueueMock<>>();
  auto wrapped = sut->Wrap([](){});

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped();
}

TEST(TaskQueue, WrapLambdaWithParam) {
  auto sut     = std::make_shared<nf7::test::TaskQueueMock<uint32_t>>();
  auto wrapped = sut->Wrap([](uint32_t){});

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped();
}

TEST(TaskQueue, WrapTask) {
  auto sut     = std::make_shared<nf7::test::TaskQueueMock<>>();
  auto wrapped = sut->Wrap(nf7::Task<> { nf7::Task<>::Time {0ms}, [](){} });

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped();
}

TEST(TaskQueue, WrapInFutureThen) {
  auto sut = std::make_shared<nf7::test::TaskQueueMock<>>();
  EXPECT_CALL(*sut, Push)
      .WillOnce([](auto&& task) { task(); });

  nf7::Future<int32_t> fut {int32_t {777}};

  auto called = uint32_t {0};
  fut.Then(sut->Wrap([&](const auto& x) {
    ++called;
    EXPECT_EQ(x, int32_t {777});
  }));

  EXPECT_EQ(called, 1);
}

TEST(TaskQueue, WrapInFutureThenWithParam) {
  auto sut = std::make_shared<nf7::test::TaskQueueMock<uint32_t>>();
  EXPECT_CALL(*sut, Push)
      .WillOnce([](auto&& task) { task(666); });

  nf7::Future<int32_t> fut {int32_t {777}};

  auto called = uint32_t {0};
  fut.Then(sut->Wrap([&](const auto& x, auto y) {
    ++called;
    EXPECT_EQ(x, int32_t {777});
    EXPECT_EQ(y, int32_t {666});
  }));

  EXPECT_EQ(called, 1);
}

TEST(SimpleTaskQueue, PushAndDrive) {
  nf7::test::SimpleTaskQueueMock<> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);
  EXPECT_CALL(sut, onErrorWhileExec).Times(0);

  auto interrupt = false;
  ::testing::NiceMock<nf7::test::SimpleTaskQueueDriverMock<>> driver;
  ON_CALL(driver, EndBusy)
      .WillByDefault([&]() { interrupt = true; });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });

  auto called = uint32_t {0};
  sut.Push(nf7::Task<> {[&](){ ++called; }});
  sut.Drive(driver);

  EXPECT_EQ(called, 1);
}

TEST(SimpleTaskQueue, PushAndDriveWithParam) {
  nf7::test::SimpleTaskQueueMock<uint32_t> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);
  EXPECT_CALL(sut, onErrorWhileExec).Times(0);

  auto interrupt = false;
  ::testing::NiceMock<nf7::test::SimpleTaskQueueDriverMock<uint32_t>> driver;
  ON_CALL(driver, EndBusy)
      .WillByDefault([&]() { interrupt = true; });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });
  ON_CALL(driver, params)
      .WillByDefault([&]() { return std::tuple<uint32_t> {666}; });

  auto called = uint32_t {0};
  sut.Push(nf7::Task<uint32_t> {[&](auto x){
    EXPECT_EQ(x, 666);
    ++called;
  }});
  sut.Drive(driver);

  EXPECT_EQ(called, 1);
}

TEST(SimpleTaskQueue, PushWithDelayAndDrive) {
  constexpr auto dur = 100ms;

  auto tick = 0ms;

  nf7::test::SimpleTaskQueueMock<> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);
  EXPECT_CALL(sut, onErrorWhileExec).Times(0);

  auto cycle = uint32_t {0};
  auto interrupt = false;
  ::testing::NiceMock<nf7::test::SimpleTaskQueueDriverMock<>> driver;
  ON_CALL(driver, BeginBusy)
      .WillByDefault([&]() {
        if (++cycle == 2) {
          tick += dur;
        }
      });
  ON_CALL(driver, tick)
      .WillByDefault([&]() { return nf7::Task<>::Time {tick}; });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });

  const auto expect_at = std::chrono::system_clock::now() + dur;
  decltype(std::chrono::system_clock::now()) actual_at;

  sut.Push(nf7::Task<> { nf7::Task<>::Time {dur}, [&](){
    actual_at = std::chrono::system_clock::now();
    interrupt = true;
  }});

  sut.Drive(driver);

  EXPECT_GE(actual_at, expect_at);
}

TEST(SimpleTaskQueue, PushWithDelayAndDriveOrderly) {
  auto tick = 0s;

  nf7::test::SimpleTaskQueueMock<> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);
  EXPECT_CALL(sut, onErrorWhileExec).Times(0);

  auto interrupt = false;
  ::testing::NiceMock<nf7::test::SimpleTaskQueueDriverMock<>> driver;
  ON_CALL(driver, EndBusy)
      .WillByDefault([&]() { interrupt = true; });
  ON_CALL(driver, tick)
      .WillByDefault([&]() { return nf7::Task<>::Time {tick}; });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });

  auto called_after       = uint32_t {0};
  auto called_immediately = uint32_t {0};
  sut.Push(nf7::Task<> {nf7::Task<>::Time {1s}, [&](){ ++called_after; }});
  sut.Push(nf7::Task<> {nf7::Task<>::Time {0s}, [&](){ ++called_immediately; }});

  interrupt = false;
  sut.Drive(driver);

  EXPECT_EQ(called_after,       0);
  EXPECT_EQ(called_immediately, 1);

  interrupt = false;
  ++tick;
  sut.Drive(driver);

  EXPECT_EQ(called_after,       1);
  EXPECT_EQ(called_immediately, 1);
}

TEST(SimpleTaskQueue, ThrowInDrive) {
  nf7::test::SimpleTaskQueueMock<> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);
  EXPECT_CALL(sut, onErrorWhileExec).Times(1);

  auto interrupt = false;
  ::testing::NiceMock<nf7::test::SimpleTaskQueueDriverMock<>> driver;
  ON_CALL(driver, EndBusy)
      .WillByDefault([&]() { interrupt = true; });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });

  auto called = uint32_t {0};
  sut.Push(nf7::Task<> {[&](){ throw nf7::Exception {"helloworld"}; }});
  sut.Push(nf7::Task<> {[&](){ ++called; }});
  sut.Drive(driver);
}

TEST(SimpleTaskQueue, ChaoticPushAndDrive) {
  constexpr auto kThreads = uint32_t {32};
  constexpr auto kPushPerThread = uint32_t {100};

  std::vector<uint32_t> values(kThreads);
  std::vector<std::thread> threads(kThreads);
  std::atomic<uint32_t> exitedThreads {0};

  nf7::test::SimpleTaskQueueMock<> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);
  EXPECT_CALL(sut, onErrorWhileExec).Times(0);

  // use NiceMock to suppress annoying warnings that slowed unittests
  ::testing::NiceMock<nf7::test::SimpleTaskQueueDriverMock<>> driver;
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return exitedThreads >= kThreads; });

  for (uint32_t i = 0; i < kThreads; ++i) {
    threads[i] = std::thread {[&, i](){
      for (uint32_t j = 0; j < kPushPerThread; ++j) {
        sut.Push(nf7::Task<> {[&, i](){ ++values[i]; }});
      }
      sut.Push(nf7::Task<> {[&](){ ++exitedThreads; }});
    }};
  }
  for (auto& th : threads) {
    th.join();
  }
  sut.Drive(driver);

  for (const auto execCount : values) {
    EXPECT_EQ(execCount, kPushPerThread);
  }
}
