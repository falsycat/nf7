// No copyright
#pragma once

#include "iface/common/task.hh"

#include <gmock/gmock.h>

#include <chrono>
#include <tuple>


namespace nf7::test {

template <typename... Args>
class TaskQueueMock : public TaskQueue<Args...> {
 public:
  using Item = Task<Args...>;

  TaskQueueMock() = default;

  MOCK_METHOD(void, Push, (Item&&), (noexcept, override));
};

template <typename... Args>
class SimpleTaskQueueMock : public SimpleTaskQueue<Args...> {
 public:
  SimpleTaskQueueMock() = default;

  MOCK_METHOD(void, onErrorWhilePush, (std::source_location), (noexcept));
  MOCK_METHOD(void, onErrorWhileExec, (std::source_location), ());
};

template <typename... Args>
class SimpleTaskQueueDriverMock : public SimpleTaskQueue<Args...>::Driver {
 public:
  using Item = Task<Args...>;
  using Time = typename Item::Time;

  SimpleTaskQueueDriverMock() = default;

  MOCK_METHOD(void, BeginBusy, (), (noexcept, override));
  MOCK_METHOD(void, EndBusy, (), (noexcept, override));
  MOCK_METHOD(Time, tick, (), (const, noexcept, override));
  MOCK_METHOD(std::tuple<Args...>, params, (), (const, noexcept, override));
  MOCK_METHOD(bool, nextIdleInterruption, (), (const, override, noexcept));
  MOCK_METHOD(bool, nextTaskInterruption, (), (const, override, noexcept));
};

}  // namespace nf7::test
