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


static_assert(nf7::TaskLike<nf7::Task<int32_t>>);
static_assert(nf7::TaskDriverLike<
                nf7::test::SimpleTaskQueueDriverMock<nf7::Task<int32_t>>,
              nf7::Task<int32_t>>);

TEST(Task, ExecAndThrow) {
  const auto line = __LINE__ + 1;
  nf7::Task<int32_t> task {[&](auto) { throw nf7::Exception {"hello"}; }};

  try {
    task(0);
    EXPECT_FALSE("unreachable (exception expected)");
  } catch (const nf7::Exception& e) {
    EXPECT_EQ(e.location().line(), line);
    EXPECT_EQ(e.location().file_name(), __FILE__);
  }
}

TEST(TaskQueue, WrapLambdaWithArgs) {
  auto sut     = std::make_shared<nf7::test::TaskQueueMock<nf7::Task<int32_t>>>();
  auto wrapped = sut->Wrap([](const char*){});

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped("hello");
}

TEST(TaskQueue, WrapLambdaWithTaskParam) {
  auto sut = std::make_shared<nf7::test::TaskQueueMock<nf7::Task<int32_t>>>();
  auto wrapped = sut->Wrap([](auto){});

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped();
}

TEST(TaskQueue, WrapLambdaWithArgsAndTaskParam) {
  auto sut     = std::make_shared<nf7::test::TaskQueueMock<nf7::Task<int32_t>>>();
  auto wrapped = sut->Wrap([](const char*, auto){});

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped("hello");
}

TEST(TaskQueue, WrapTask) {
  auto sut     = std::make_shared<nf7::test::TaskQueueMock<nf7::Task<int32_t>>>();
  auto wrapped = sut->Wrap(nf7::Task<int32_t> { [](auto){} });

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped();
}

TEST(TaskQueue, WrapTaskWithRef) {
  auto sut = std::make_shared<nf7::test::TaskQueueMock<nf7::Task<int32_t&>>>();

  auto wrapped = sut->Wrap(nf7::Task<int32_t&> { [](auto&){} });

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped();
}

TEST(TaskQueue, WrapInFutureThen) {
  auto sut = std::make_shared<nf7::test::TaskQueueMock<nf7::Task<int32_t>>>();
  EXPECT_CALL(*sut, Push)
      .WillOnce([](auto&& task) { task(int32_t {666}); });

  nf7::Future<int32_t> fut {int32_t {777}};

  auto called = uint32_t {0};
  fut.Then(sut->Wrap([&](const auto& x) {
    ++called;
    EXPECT_EQ(x, int32_t {777});
  }));

  EXPECT_EQ(called, uint32_t {1});
}

TEST(TaskQueue, WrapInFutureThenWithTaskParam) {
  auto sut = std::make_shared<nf7::test::TaskQueueMock<nf7::Task<int32_t>>>();
  EXPECT_CALL(*sut, Push)
      .WillOnce([](auto&& task) { task(int32_t {666}); });

  nf7::Future<int32_t> fut {int32_t {777}};

  auto called = uint32_t {0};
  fut.Then(sut->Wrap([&](const auto& x, auto y) {
    ++called;
    EXPECT_EQ(x, int32_t {777});
    EXPECT_EQ(y, int32_t {666});
  }));

  EXPECT_EQ(called, uint32_t {1});
}

TEST(WrappedTaskQueue, Push) {
  auto base = std::make_unique<nf7::test::TaskQueueMock<nf7::Task<int32_t>>>();
  EXPECT_CALL(*base, Push).Times(1);

  class A : public nf7::TaskQueue<nf7::Task<int32_t>> {
   public:
    A() = default;
  };
  nf7::WrappedTaskQueue<A> sut {std::move(base)};
  static_assert(std::is_base_of_v<A, decltype(sut)>,
                "WrappedTaskQueue doesn't based on base type");

  sut.Push(nf7::Task<int32_t> {[](auto){}});

  // ensure all templates legal
  (std::void_t<decltype(sut.Wrap([](auto){}))>) 0;
  (std::void_t<decltype(sut.ExecAnd<uint32_t>([](auto){ return 0; }))>) 0;
  (std::void_t<decltype(sut.Exec([](auto){}))>) 0;
}

TEST(SimpleTaskQueue, PushAndDrive) {
  nf7::test::SimpleTaskQueueMock<nf7::Task<int32_t&>> sut;
  nf7::test::SimpleTaskQueueDriverMock<nf7::Task<int32_t&>> driver;

  EXPECT_CALL(sut, onErrorWhilePush).Times(0);

  auto interrupt = false;
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });

  auto ctx = int32_t {0};

  ::testing::Sequence s;
  EXPECT_CALL(driver, BeginBusy)
      .Times(1)
      .InSequence(s);
  EXPECT_CALL(driver, Drive)
      .Times(1)
      .InSequence(s)
      .WillOnce([&](auto&& task) { task(ctx); });
  EXPECT_CALL(driver, EndBusy)
      .Times(1)
      .InSequence(s)
      .WillOnce([&]() { interrupt = true; });

  auto called = uint32_t {0};
  sut.Exec([&](auto&){ ++called; });
  sut.Drive(driver);

  EXPECT_EQ(called, 1);
}

TEST(SimpleTaskQueue, PushWithDelayAndDrive) {
  constexpr auto dur = 100ms;

  nf7::test::SimpleTaskQueueMock<nf7::Task<int32_t&>> sut;
  nf7::test::SimpleTaskQueueDriverMock<nf7::Task<int32_t&>> driver;

  EXPECT_CALL(sut, onErrorWhilePush).Times(0);

  auto tick      = 0ms;
  auto ctx       = int32_t {0};
  auto cycle     = uint32_t {0};
  auto interrupt = false;

  ON_CALL(driver, BeginBusy)
      .WillByDefault([&]() {
        if (++cycle == 2) {
          tick += dur;
        }
      });
  ON_CALL(driver, Drive)
      .WillByDefault([&](auto&& task) { task(ctx); });
  ON_CALL(driver, tick)
      .WillByDefault([&]() { return nf7::Task<int32_t&>::Time {tick}; });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });

  const auto expect_at = std::chrono::system_clock::now() + dur;
  decltype(std::chrono::system_clock::now()) actual_at;

  sut.Push(nf7::Task<int32_t&> { nf7::Task<int32_t>::Time {dur}, [&](auto&){
    actual_at = std::chrono::system_clock::now();
    interrupt = true;
  }});

  sut.Drive(driver);

  EXPECT_GE(actual_at, expect_at);
}

TEST(SimpleTaskQueue, PushWithDelayAndDriveOrderly) {
  using Task = nf7::Task<int32_t&>;

  nf7::test::SimpleTaskQueueMock<Task> sut;
  nf7::test::SimpleTaskQueueDriverMock<Task> driver;

  EXPECT_CALL(sut, onErrorWhilePush).Times(0);

  auto tick      = 0s;
  auto interrupt = false;
  auto ctx       = int32_t {0};

  ON_CALL(driver, Drive)
      .WillByDefault([&](auto&& task) { task(ctx); });
  ON_CALL(driver, EndBusy)
      .WillByDefault([&]() { interrupt = true; });
  ON_CALL(driver, tick)
      .WillByDefault([&]() { return Task::Time {tick}; });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return interrupt; });

  auto called_after       = uint32_t {0};
  auto called_immediately = uint32_t {0};
  sut.Push(Task {Task::Time {1s}, [&](auto&){ ++called_after; }});
  sut.Push(Task {Task::Time {0s}, [&](auto&){ ++called_immediately; }});

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

TEST(SimpleTaskQueue, ChaoticPushAndDrive) {
  constexpr auto kThreads = uint32_t {32};
  constexpr auto kPushPerThread = uint32_t {100};

  nf7::test::SimpleTaskQueueMock<nf7::Task<int32_t&>> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);

  // use NiceMock to suppress annoying warnings that slowed unittests
  ::testing::NiceMock<
      nf7::test::SimpleTaskQueueDriverMock<nf7::Task<int32_t&>>> driver;

  auto ctx           = int32_t                  {0};
  auto values        = std::vector<uint32_t>    (kThreads);
  auto threads       = std::vector<std::thread> (kThreads);
  auto exitedThreads = std::atomic<uint32_t>    {0};

  ON_CALL(driver, Drive)
      .WillByDefault([&](auto&& task) { task(ctx); });
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() { return exitedThreads >= kThreads; });

  for (uint32_t i = 0; i < kThreads; ++i) {
    threads[i] = std::thread {[&, i](){
      for (uint32_t j = 0; j < kPushPerThread; ++j) {
        sut.Exec([&, i](auto&){ ++values[i]; });
      }
      sut.Exec([&](auto&){ ++exitedThreads; });
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

TEST(SimpleTaskQueue, WaitForEmpty) {
  nf7::test::SimpleTaskQueueMock<nf7::Task<int32_t&>> sut;
  EXPECT_CALL(sut, onErrorWhilePush).Times(0);

  // use NiceMock to suppress annoying warnings that slowed unittests
  ::testing::NiceMock<
      nf7::test::SimpleTaskQueueDriverMock<nf7::Task<int32_t&>>> driver;

  for (uint32_t i = 0; i < 1000; ++i) {
    sut.Exec([](auto&){});
  }

  auto ctx = int32_t {0};
  ON_CALL(driver, Drive)
      .WillByDefault([&](auto&& task) { task(ctx); });

  std::atomic<bool> exit = false;
  ON_CALL(driver, nextIdleInterruption)
      .WillByDefault([&]() -> bool { return exit; });

  std::thread th {[&]() { sut.Drive(driver); }};
  EXPECT_TRUE(sut.WaitForEmpty(1s));

  exit = true;
  sut.Wake();
  th.join();
}

TEST(SimpleTaskQueue, WaitForEmptyWhenEmpty) {
  nf7::test::SimpleTaskQueueMock<nf7::Task<int32_t&>> sut;
  EXPECT_TRUE(sut.WaitForEmpty(1s));
}
