// No copyright
#include "iface/common/task.hh"
#include "iface/common/task_test.hh"

#include <gtest/gtest.h>

#include "iface/common/future.hh"


TEST(Task, RunAndThrow) {
  const auto line = __LINE__ + 1;
  nf7::Task task {[&]() { throw nf7::Exception {"hello"}; }};

  try {
    task.Run();
    EXPECT_FALSE("unreachable (exception expected)");
  } catch (const nf7::Exception& e) {
    EXPECT_EQ(e.location().line(), line);
    EXPECT_EQ(e.location().file_name(), __FILE__);
  }
}

TEST(TaskQueue, Wrap) {
  auto sut     = std::make_shared<nf7::test::TaskQueueMock>();
  auto wrapped = sut->Wrap([](){});

  EXPECT_CALL(*sut, Push(::testing::_)).Times(1);

  wrapped();
}

TEST(TaskQueue, WrapInFutureThen) {
  auto sut = std::make_shared<nf7::test::TaskQueueMock>();
  ON_CALL(*sut, Push(::testing::_)).WillByDefault([](auto&& task) {
    task.Run();
  });

  nf7::Future<int32_t> fut {int32_t {777}};

  auto called = uint32_t {0};
  fut.Then(sut->Wrap([&](auto& x) {
    ++called;
    EXPECT_EQ(x, int32_t {777});
  }));

  EXPECT_EQ(called, 1);
}
