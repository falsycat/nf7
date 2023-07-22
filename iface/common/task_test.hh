// No copyright
#pragma once

#include "iface/common/task.hh"

#include <gmock/gmock.h>

#include <chrono>


namespace nf7::test {

class TaskQueueMock : public TaskQueue {
 public:
  TaskQueueMock() = default;

  MOCK_METHOD(void, Push, (Task&&), (noexcept, override));
};

class SimpleTaskQueueMock : public SimpleTaskQueue {
 public:
  SimpleTaskQueueMock() = default;

  MOCK_METHOD(void, onErrorWhilePush, (std::source_location), (noexcept));
  MOCK_METHOD(void, onErrorWhileExec, (std::source_location), ());
};

class SimpleTaskQueueDriverMock : public SimpleTaskQueue::Driver {
 public:
  SimpleTaskQueueDriverMock() = default;

  MOCK_METHOD(void, BeginBusy, (), (noexcept, override));
  MOCK_METHOD(void, EndBusy, (), (noexcept, override));
  MOCK_METHOD(Task::Time, tick, (), (const, noexcept, override));
  MOCK_METHOD(bool, nextIdleInterruption, (), (const, override, noexcept));
  MOCK_METHOD(bool, nextTaskInterruption, (), (const, override, noexcept));
};

}  // namespace nf7::test
