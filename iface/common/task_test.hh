// No copyright
#pragma once

#include "iface/common/task.hh"

#include <gmock/gmock.h>

#include <chrono>
#include <tuple>


namespace nf7::test {

template <TaskLike T>
class TaskQueueMock : public TaskQueue<T> {
 public:
  using Item = T;

  TaskQueueMock() = default;

  MOCK_METHOD(void, Push, (Item&&), (noexcept, override));
};

template <TaskLike T>
class SimpleTaskQueueMock : public SimpleTaskQueue<T> {
 public:
  SimpleTaskQueueMock() = default;

  MOCK_METHOD(void, onErrorWhilePush, (std::source_location), (noexcept));
};

template <TaskLike T>
class SimpleTaskQueueDriverMock {
 public:
  using Item = T;
  using Time = typename Item::Time;

  SimpleTaskQueueDriverMock() = default;

  SimpleTaskQueueDriverMock(const SimpleTaskQueueDriverMock&) = delete;
  SimpleTaskQueueDriverMock(SimpleTaskQueueDriverMock&&) = delete;
  SimpleTaskQueueDriverMock& operator=(const SimpleTaskQueueDriverMock&) = delete;
  SimpleTaskQueueDriverMock& operator=(SimpleTaskQueueDriverMock&&) = delete;

  MOCK_METHOD(void, BeginBusy, (), (noexcept));
  MOCK_METHOD(void, Drive, (T&&), (noexcept));
  MOCK_METHOD(void, EndBusy, (), (noexcept));
  MOCK_METHOD(Time, tick, (), (const, noexcept));
  MOCK_METHOD(bool, nextIdleInterruption, (), (const, noexcept));
  MOCK_METHOD(bool, nextTaskInterruption, (), (const, noexcept));
};

}  // namespace nf7::test
